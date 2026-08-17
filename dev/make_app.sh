#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# Packages the already-built st/pt binaries into macOS .app bundles. Nothing
# is compiled here; the binary is copied in as-is. platform_cocoa.mm already
# runs a full NSApplication and sets the Dock icon at startup, but without a
# bundle the menu bar shows argv[0] ("st") and there is no Info.plist to hang
# an icon or identifier off of. This closes that gap for distribution.
set -e

BIN_DIR="${1:-.build}"
OUT_DIR="${2:-.build-app}"
VERSION="${3:-$(git describe --tags --always 2>/dev/null || echo 0.0.0)}"

# The SVGs live under bin/st and bin/pt regardless of the caller's cwd.
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

mkdir -p "$OUT_DIR"

# Cleans up after a failed run: the iconset workdir (only set while make_icns
# is rendering) and a half-built .app (CURRENT_APP is cleared only once
# codesign succeeds, so a crash anywhere before that — including on codesign
# itself, e.g. a PATH collision copying in the wrong binary — never leaves
# something in $OUT_DIR that looks like a finished bundle).
WORK=""
CURRENT_APP=""
cleanup() {
    STATUS=$?
    [ -n "$WORK" ] && rm -rf "$WORK"
    [ -n "$CURRENT_APP" ] && rm -rf "$CURRENT_APP"
    exit "$STATUS"
}
trap cleanup EXIT

# ./build's default output is .build (see build:2475); fall back to an
# installed binary so this also works straight off a `brew install`.
find_binary() {
    if [ -x "$BIN_DIR/$1" ]; then
        echo "$BIN_DIR/$1"
    elif command -v "$1" >/dev/null 2>&1; then
        command -v "$1"
    else
        echo "error: '$1' not found in $BIN_DIR or PATH" >&2
        exit 1
    fi
}

# Renders an .icns from the per-brand bin/*/SVG using the same rasterizer
# build.py's icon_png() relies on (build.py:501). Without rsvg-convert the
# bundle still works — platform_cocoa.mm sets the Dock icon at runtime
# regardless — so this warns instead of failing the whole packaging step.
make_icns() {
    SVG="$1"
    ICNS_OUT="$2"
    if ! command -v rsvg-convert >/dev/null 2>&1; then
        echo "warning: rsvg-convert not found, packaging $(basename "$ICNS_OUT") without an icon (Dock icon is still set at runtime)" >&2
        return 0
    fi
    WORK="$(mktemp -d)"
    ICONSET="$WORK/icon.iconset"
    mkdir -p "$ICONSET"
    for SIZE in 16 32 128 256 512; do
        rsvg-convert -w "$SIZE" -h "$SIZE" "$SVG" -o "$ICONSET/icon_${SIZE}x${SIZE}.png"
        DOUBLE=$((SIZE * 2))
        rsvg-convert -w "$DOUBLE" -h "$DOUBLE" "$SVG" -o "$ICONSET/icon_${SIZE}x${SIZE}@2x.png"
    done
    iconutil -c icns "$ICONSET" -o "$ICNS_OUT"
    rm -rf "$WORK"
    WORK=""
}

# Assembles one bundle. Name/executable/identifier/icon vary per brand, the
# Contents/{Info.plist,MacOS,Resources} shape does not.
make_bundle() {
    NAME="$1"
    EXE="$2"
    IDENT="$3"
    SVG="$4"

    APP="$OUT_DIR/$NAME.app"
    CURRENT_APP="$APP"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

    cp "$(find_binary "$EXE")" "$APP/Contents/MacOS/$EXE"

    ICON_BLOCK=""
    if [ -f "$SVG" ]; then
        make_icns "$SVG" "$APP/Contents/Resources/$IDENT.icns"
        if [ -f "$APP/Contents/Resources/$IDENT.icns" ]; then
            ICON_BLOCK="    <key>CFBundleIconFile</key>
    <string>$IDENT.icns</string>
"
        fi
    fi

    cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$EXE</string>
    <key>CFBundleName</key>
    <string>$NAME</string>
    <key>CFBundleDisplayName</key>
    <string>$NAME</string>
    <key>CFBundleIdentifier</key>
    <string>org.pg83.$IDENT</string>
$ICON_BLOCK    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

    # Ad-hoc signature: macOS refuses to run an unsigned .app cleanly.
    # Notarization needs a Developer ID, which is a distribution decision
    # left to the caller, not this script.
    codesign -s - --force "$APP"
    CURRENT_APP=""
}

make_bundle Shitty st shitty "$ROOT_DIR/bin/st/shitty.svg"
make_bundle Pretty pt pretty "$ROOT_DIR/bin/pt/pretty.svg"

echo "built $OUT_DIR/Shitty.app and $OUT_DIR/Pretty.app"
