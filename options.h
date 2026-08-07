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

// Every string lives in the ObjPool the instance was created in, NUL
// terminated, so a view's data() doubles as a C string for the libc
// calls that need one.
struct Options {
    u8 fontsize = 0;
    u8 modifyOtherKeys = 0;
    u16 border = 0;
    u16 nCols = 0;
    u16 nRows = 0;
    u16 saveLines = 0;
    // Unicode major version the cell widths emulate; 0 matches the
    // system libc by probing its wcwidth at startup.
    u16 unicodeWidths = 0;
    stl::Vector<stl::StringView> fontnames;
    stl::Vector<stl::StringView> remaps;
    stl::Vector<stl::StringView> uriSchemes;
    Darts* uriSchemeTrie = nullptr;
    stl::StringView shell;
    stl::StringView title;
    stl::StringView dump;
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
    bool login = false;
    bool noDecorations = false;
    bool showWraps = false;
    bool rv = false;
    bool verbose = false;

    // Whether a detected plain URI with this scheme, in any case, may be
    // presented as openable.
    bool uriSchemeAllowed(stl::StringView scheme) const;

    static Options* create(stl::ObjPool& pool, Brand& brand, char** argv, int argc);
};
