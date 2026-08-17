#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# Wraps dev/make_app.sh's output into one zip per brand for release. `ditto
# -c -k --keepParent` is the macOS-native way to zip a .app bundle: unlike
# the `zip` tool, it preserves file permissions and symlinks and, crucially,
# the ad-hoc code signature make_app.sh applies, so a downloaded zip unpacks
# to a bundle that still passes `codesign -dv`.
set -e

BIN_DIR="${1:-.build-darwin}"
APP_DIR="${2:-.build-app}"
ZIP_DIR="${3:-.build-app-zip}"

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

"$ROOT_DIR/dev/make_app.sh" "$BIN_DIR" "$APP_DIR"

mkdir -p "$ZIP_DIR"
for NAME in Shitty Pretty; do
    ZIP="$ZIP_DIR/$NAME.app.zip"
    rm -f "$ZIP"
    ditto -c -k --keepParent "$APP_DIR/$NAME.app" "$ZIP"
done

echo "packaged $ZIP_DIR/Shitty.app.zip and $ZIP_DIR/Pretty.app.zip"
