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

#include "ansi_palette.h"
#include "unicode_width.h"

#include <plt/window.h>

#include <std/lib/vector.h>
#include <std/str/view.h>
#include <std/sys/types.h>

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

// Every string lives in the ObjPool the instance was created in, NUL
// terminated, so a view's data() doubles as a C string for the libc
// calls that need one.
struct Options {
    u8 fontsize = 0;
    // -1: classic hinted grid rendering. 0..100: unhinted rendering with
    // subpixel glyph placement, the value scaling the stem darkening.
    i8 soft = -1;
    u8 modifyOtherKeys = 0;
    u16 border = 0;
    u16 nCols = 0;
    u16 nRows = 0;
    u16 saveLines = 0;
    // Quick-terminal window corner radius, in points; 0 disables rounding.
    // Parsed and range-checked here; threaded into plt::WindowOptions and
    // consumed by the Cocoa window layer by T3, which is the only reader.
    u16 quickCornerRadius = 0;
    // Width of the sidebar tab list, in points, when -tabBar is sidebar.
    // Reserved into Composer::contentInsets().left by ui_sidebar_tabs.mm.
    u16 sidebarWidth = 0;
    // The width emulation resolved from -unicodeWidths; parsing probes
    // the system libc when the option asks to match it.
    UnicodeWidths widths{0};
    stl::Vector<stl::StringView> fontnames;
    stl::Vector<stl::StringView> remaps;
    stl::Vector<stl::StringView> uriSchemes;
    Darts* uriSchemeTrie = nullptr;
    stl::StringView shell;
    stl::StringView title;
    stl::StringView dump;
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
    Color bg{};
    Color cr{};
    Color fg{};
    AnsiPalette palette{};
    bool altScrollMode = false;
    bool altSendsEscape = false;
    bool autoCopyMode = false;
    bool allowOsc52Read = false;
    bool allowWindowOps = false;
    bool osc52SelectClipboard = false;
    bool boldColors = false;
    bool kittyCtrlBaseLayout = false;
    bool vulkanInfo = false;
    // Skip the direct-storage swapchain even where the surface offers
    // it: the CI shadow renderer walks the blit fallback this way.
    bool vulkanBlit = false;
    bool login = false;
    bool maximized = false;
    // The macOS natural-text-editing preset: Option word gestures and
    // Command line gestures as chords, at the price of the reserved
    // Command arrows.
    bool naturalEditing = false;
    bool noDecorations = false;
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
    bool transparentTitlebar = false;
    bool rv = false;
    bool verbose = false;

    // Whether a detected plain URI with this scheme, in any case, may be
    // presented as openable.
    bool uriSchemeAllowed(stl::StringView scheme) const;

    static Options* create(stl::ObjPool& pool, Brand& brand, char** argv, int argc, OptionsLoad load = OptionsLoad::Startup);
};
