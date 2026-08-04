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
#include <std/sys/types.h>

#include "color.h"

#include <cstdint>

namespace stl {
    class ObjPool;
}

enum class OptionSource {
    NONE,
    HardDefault,
    Config,
    CmdLine
};

struct Options {
    static Options* create(stl::ObjPool& pool, char** argv, int argc);

    u8 fontsize = 0;
    u8 modifyOtherKeys = 0;
    u16 border = 0;
    u16 nCols = 0;
    u16 nRows = 0;
    u16 saveLines = 0;
    const char* const* fontnames = nullptr;
    size_t fontnameCount = 0;
    const char* const* remaps = nullptr;
    size_t remapCount = 0;
    const char* shell = nullptr;
    const char* title = nullptr;
    const char* dump = nullptr;
    OptionSource titleSource = OptionSource::NONE;
    Color bg{};
    Color cr{};
    Color fg{};
    Color palette[16]{};
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
};
