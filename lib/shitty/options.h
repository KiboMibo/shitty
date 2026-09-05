/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */
/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#pragma once

#include <lib/vterm/vt_config.h>
#include <lib/vterm/ansi_palette.h>

#include <std/str/view.h>
#include <std/sys/types.h>
#include <std/lib/vector.h>

#include <plt/window.h>

namespace stl {
    class ObjPool;
}

struct Darts;
struct Brand;

enum class OptionSource {
    NONE,
    HardDefault,
    Config,
    CmdLine
};

enum class OptionsLoad {
    Startup,
    Reload
};

// What -backgroundBlur asks the window to put behind a translucent
// background. Off creates no backdrop at all; Blur is the frosted
// NSVisualEffectView this terminal has had since T10; Glass is the
// macOS 26 system glass, which falls back to Blur where the system has
// none.
//
// The numeric values are not a contract - the test-mode dump prints the
// name for exactly that reason - so a mode may be inserted anywhere.
enum class BackdropMode: u8 {
    Off,
    Blur,
    Glass
};

// The spelling the config, the command line and the test-mode dump all
// share. One table, so a mode cannot be accepted under a name nothing
// ever prints back.
stl::StringView backdropModeName(BackdropMode mode);

// One [[symbolFont]] config entry: inside [first, last] the named font
// is consulted before the regular fallback chain. Entries are tried in
// document order; a font that does not cover the cluster falls through
// to the next matching entry and then to the ordinary chain.
struct SymbolFontSpan {
    u32 first = 0;
    u32 last = 0;
    stl::StringView font;
};

// Every string lives in the ObjPool the instance was created in, NUL
// terminated, so a view's data() doubles as a C string for the libc
// calls that need one.
struct Options {
    // The semantic knobs of the VT core live in the embedded VtConfig;
    // everything else here is the interactive shell around it.
    VtConfig vt;
    u8 fontsize = 0;
    // -1: classic hinted grid rendering. 0..100: unhinted rendering with
    // subpixel glyph placement, the value scaling the stem darkening.
    i8 soft = -1;
    // How much of the desktop shows through the terminal background, as a
    // percentage of opaque: 100 keeps today's solid window, 0 leaves the
    // background invisible. Only the *background* follows it - glyphs,
    // the cursor, a selection and the pane divider stay solid, because a
    // terminal whose letters are see-through is unreadable.
    //
    // Read once, at startup, on both sides of the decision: the Cocoa
    // window is made transparent at creation time from this value
    // (platform_cocoa.mm), and the renderer will not write alpha into a
    // layer that was created opaque. A reload that raises or lowers it
    // within a window that started translucent takes effect; one that
    // asks an opaque window to become translucent needs a restart. The
    // alternative - letting the renderer act on a reload the window
    // cannot follow - paints the background darker instead of
    // see-through, which is a wrong picture rather than an unchanged one.
    u16 backgroundOpacity = 100;
    u16 border = 0;
    // The seam between two panes, in pixels, and what colour it is.
    // A10's default used to be a zero gap - panes touching, their own
    // borders making the air between them - which left nothing to see
    // and nothing to aim at. One pixel is the smallest thing that is
    // still a line; the grab strip is a separate number and does not
    // follow this one (see SessionSetImpl::dividerGrab).
    u16 paneDividerWidth = 1;
    u16 nCols = 0;
    u16 nRows = 0;
    // Quick-terminal window corner radius, in points; 0 disables rounding.
    // Parsed and range-checked here; threaded into plt::WindowOptions and
    // consumed by the Cocoa window layer by T3, which is the only reader.
    u16 quickCornerRadius = 0;
    // Width of the sidebar tab list, in points, when -tabBar is sidebar.
    // Reserved into Composer::contentInsets().left by ui_sidebar_tabs.mm.
    u16 sidebarWidth = 0;
    stl::Vector<stl::StringView> fontnames;
    // TOML-only ([[symbolFont]] tables); there is no command-line form.
    stl::Vector<SymbolFontSpan> symbolFonts;
    stl::Vector<stl::StringView> remaps;
    stl::Vector<stl::StringView> uriSchemes;
    // The lowercased spellings of uriSchemes, interned as a trie at
    // parse time; the host adapter answers scheme policy from it.
    const Darts* uriSchemeTrie = nullptr;
    stl::StringView shell;
    // -debug: append window/font/grid diagnostics to this file.
    stl::StringView debugTrace;
    // The chord that toggles the quick-terminal window; only parsed and
    // validated non-empty here, the chord grammar itself is T3's.
    stl::StringView quickHotkey;
    // Path to a config file for a quick-terminal companion process this
    // one spawns and manages; empty runs without one. Only stored here -
    // ~ expansion, realpath canonicalization, the self-reference guard,
    // and the fork/exec itself all live in quick_companion.cpp, which
    // needs argv0 and the filesystem the option parser does not have.
    stl::StringView quickCompanion;
    // The chord that toggles quick-terminal window fullscreen. Only parsed
    // and stored here, same as quickHotkey above; empty means disabled.
    // Chord grammar and registration are T3's, in the same module as
    // quickHotkey (ui_quick_hotkey.mm).
    stl::StringView quickFullscreenHotkey;
    // The main config file OptionsParser::loadConfigFile() resolved for
    // this process - set whether or not the file actually exists, empty
    // when no config path could even be computed (no -config, no HOME,
    // no XDG_CONFIG_HOME). quick_companion.cpp's self-reference guard
    // canonicalizes this and compares it against quickCompanion's own
    // target; it is otherwise unused.
    stl::StringView configPath;
    // The quick-terminal window's size and position, parsed from
    // -quickGeometry by lib/shitty/quick_geometry.cpp. Defaults to the
    // rect ShowPlacement::TopOfActiveScreen used before this option
    // existed: full screen width, top-aligned, 40% height.
    plt::QuickGeometry quickGeometry;
    OptionSource titleSource = OptionSource::NONE;
    // Defaults to the scheme's bright black, the way cr defaults to fg:
    // derived from whatever theme is in force rather than a constant, so
    // a light scheme gets a light seam without anyone saying so.
    Color paneDividerColor{};
    // C10. The sidebar panel's colour, and the origin every other shade
    // in the panel is mixed from - a panel whose background is set by
    // hand and whose active-row highlight is still derived from the
    // terminal's can drift apart until neither reads.
    //
    // Unset is not a colour but an absence: the panel then keeps
    // deriving itself from bg and fg exactly as it did before this
    // option existed, because that derivation is AppKit's and cannot be
    // reproduced here byte for byte. sidebarColorSet is what says which.
    Color sidebarColor{};
    bool vulkanInfo = false;
    // Skip the direct-storage swapchain even where the surface offers
    // it: the CI shadow renderer walks the blit fallback this way.
    bool vulkanBlit = false;
    bool login = false;
    bool maximized = false;
    // Fullscreen wins over maximized when both are set: it is the
    // stronger request, and the window manager would otherwise
    // resolve the pair for us differently on every platform.
    bool fullscreen = false;
    // The macOS natural-text-editing preset: Option word gestures and
    // Command line gestures as chords, at the price of the reserved
    // Command arrows.
    bool naturalEditing = false;
    bool noDecorations = false;
    // -titleFallback process: the active terminal's title follows the
    // pty's foreground process name whenever the name changes and no
    // fresher application title replaces it.
    bool titleFallbackProcess = false;
    bool optical = false;
    // Runs as a quick-terminal window: hidden at startup, shown and
    // hidden by the quickHotkey chord instead of the normal show-on-start.
    bool quick = false;
    // Persist the quick-terminal window's manually set position and size
    // across shows, via lib/shitty/quick_frame_store.{h,cpp} (T2). Parsed
    // here; the save/restore path itself is T3's.
    bool quickRememberFrame = false;
    // Where the tab bar lives, resolved from -tabBar: false is the
    // title bar (the default), true is a vertical list down the window's
    // left edge reserving sidebarWidth out of the grid. One placement or
    // the other, never both - which is the whole of what cmd+b used to
    // get wrong by swapping between them (V3).
    bool sidebarTabs = false;
    // Hide the titlebar chrome and reveal it on mouse hover, without
    // changing the grid's row count (A7). Unused until T6.
    bool autoHideChrome = false;
    // Allow splitting a tab's terminal into multiple panes (cmd+d /
    // cmd+shift+d). Unused until T9/T10 build the pane tree.
    bool panes = false;
    bool showWraps = false;
    // The titlebar's color matches the terminal background instead of
    // the system chrome color. Geometry is untouched: no
    // FullSizeContentView, the content area stays below the titlebar.
    // What to put behind whatever shows through the translucent
    // background: nothing, iTerm2's frosted blur, or system glass.
    // Meaningless while backgroundOpacity is 100 - an
    // opaque background covers the blurred backdrop completely - and in
    // that case the backdrop is simply never created rather than the
    // option being rejected: backgroundOpacity is reloadable, and a
    // config that is legal at one of its values and fatal at another
    // turns a one-line edit into a refusal to start. Cocoa-only.
    BackdropMode backgroundBlur = BackdropMode::Off;
    bool sidebarColorSet = false;
    bool transparentTitlebar = false;
    bool rv = false;

    static Options* create(stl::ObjPool& pool, Brand& brand, char** argv, int argc, OptionsLoad load = OptionsLoad::Startup);

    // Case-folds the scheme and answers from uriSchemeTrie; false until
    // the trie exists, so an unparsed instance allows nothing.
    bool uriSchemeAllowed(stl::StringView scheme) const;
};
