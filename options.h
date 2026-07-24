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

#include "base.h"

#include <cstdint>
#include <string>

enum class OptionSource {
    NONE,
    HardDefault,
    CmdLine
};

struct Options {
    u8 fontsize;
    u8 modifyOtherKeys;
    u16 border;
    u16 nCols;
    u16 nRows;
    u16 saveLines;
    const char* dwfontname;
    const char* fontname;
    const char* shell;
    const char* title;
    const char* dump;
    const char* printerCommand;
    OptionSource titleSource = OptionSource::NONE;
    Color bg;
    Color cr;
    Color fg;
    bool altScrollMode;
    bool altSendsEscape;
    bool autoCopyMode;
    bool allowOsc52Read;
    bool allowWindowOps;
    bool osc52SelectClipboard;
    bool boldColors;
    bool vulkanInfo;
    bool login;
    bool showWraps;
    bool rv;
    bool verbose;

    void initialize(int* argc, char** argv);
    void handlePrintOpts();
    void parse();

    void printVersion() const;
    void printUsage() const;
    void printResources() const;

    bool getBool(const char* name, bool defaultValue = false);
    void getColor(const char* name, Color& outColor);
    int getInteger(const char* name, int min, int max);
};

extern Options opts;
