/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once

#include "base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zutty {
    enum class OptionKind {
        NoArg,
        SepArg,
        SkipLine
    };

    struct OptionDesc {
        const char* option;
        OptionKind parseType;
        const char* implValue;
        const char* hardDefault;
        const char* helpDescr;
    };

    struct ResourceDesc {
        const char* resource;
        const char* hardDefault;
        const char* helpDescr;
    };

#if defined(FREEBSD)
    static constexpr const char* fontpath = "/usr/local/share/fonts";
#elif defined(OPENBSD)
    static constexpr const char* fontpath = "/usr/local/share/fonts";
#else
    static constexpr const char* fontpath = "/usr/share/fonts";
#endif

    static const std::vector<OptionDesc> optionsTable = {
        // option       parseType            implValue hardDefault helpDescr
        {"altScroll", OptionKind::NoArg, "true", "false", "Alternate scroll mode"},
        {"autoCopy", OptionKind::NoArg, "true", "false", "Sync primary to clipboard"},
        {"bg", OptionKind::SepArg, nullptr, "#000", "Background color"},
        {"boldColors", OptionKind::NoArg, "true", "true", "Enable bright for bold"},
        {"border", OptionKind::SepArg, nullptr, "2", "Border width in pixels"},
        {"cr", OptionKind::SepArg, nullptr, nullptr, "Cursor color"},
        {"dwfont", OptionKind::SepArg, nullptr, "18x18ja", "Double-width font to use"},
        {"fg", OptionKind::SepArg, nullptr, "#fff", "Foreground color"},
        {"font", OptionKind::SepArg, nullptr, "9x18", "Font to use"},
        {"fontsize", OptionKind::SepArg, nullptr, "16", "Font size"},
        {"fontpath", OptionKind::SepArg, nullptr, fontpath, "Font search path"},
        {"geometry", OptionKind::SepArg, nullptr, "80x24", "Terminal size in chars"},
        {"vulkanInfo", OptionKind::NoArg, "true", "false", "Print Vulkan information"},
        {"help", OptionKind::NoArg, "true", "false", "Print usage listing and quit"},
        {"listres", OptionKind::NoArg, "true", "false", "Print advanced option listing and quit"},
        {"login", OptionKind::NoArg, "true", "false", "Start shell as a login shell"},
        {"rv", OptionKind::NoArg, "true", "false", "Reverse video"},
        {"saveLines", OptionKind::SepArg, nullptr, "500", "Lines of scrollback history"},
        {"shell", OptionKind::SepArg, nullptr, nullptr, "Shell program to run"},
        {"showWraps", OptionKind::NoArg, "true", "false", "Show wrap marks at right margin"},
        {"title", OptionKind::SepArg, nullptr, "Zutty", "Window title"},
        {"quiet", OptionKind::NoArg, "true", "false", "Silence logging output"},
        {"verbose", OptionKind::NoArg, "true", "false", "Output info messages"},
        {"e", OptionKind::SkipLine, nullptr, nullptr, "Command line to run"},
    };

    static const std::vector<ResourceDesc> resourceTable = {
        // resource           hardDefault    helpDescr
        {"altSendsEscape", "true", "Encode Alt key as ESC prefix"},
        {"modifyOtherKeys", "1", "Key modifier encoding level; 0..2"},
        {"color0", "#000000", "Palette color 0"},
        {"color1", "#cd0000", "Palette color 1"},
        {"color2", "#00cd00", "Palette color 2"},
        {"color3", "#cdcd00", "Palette color 3"},
        {"color4", "#0000ee", "Palette color 4"},
        {"color5", "#cd00cd", "Palette color 5"},
        {"color6", "#00cdcd", "Palette color 6"},
        {"color7", "#e5e5e5", "Palette color 7"},
        {"color8", "#7f7f7f", "Palette color 8"},
        {"color9", "#ff0000", "Palette color 9"},
        {"color10", "#00ff00", "Palette color 10"},
        {"color11", "#ffff00", "Palette color 11"},
        {"color12", "#5c5cff", "Palette color 12"},
        {"color13", "#ff00ff", "Palette color 13"},
        {"color14", "#00ffff", "Palette color 14"},
        {"color15", "#ffffff", "Palette color 15"},
    };

    enum class OptionSource {
        NONE,
        HardDefault,
        CmdLine
    };

    struct Options {
        // N.B.: no static initializers - parse() decodes the defaults above.
        uint8_t fontsize;
        uint8_t modifyOtherKeys;
        uint16_t border;
        uint16_t nCols;
        uint16_t nRows;
        uint16_t saveLines;
        const char* dwfontname;
        const char* fontname;
        const char* fontpath;
        const char* shell;
        const char* title;
        OptionSource titleSource = OptionSource::NONE;
        Color bg;
        Color cr;
        Color fg;
        bool altScrollMode;
        bool altSendsEscape;
        bool autoCopyMode;
        bool boldColors;
        bool vulkanInfo;
        bool login;
        bool showWraps;
        bool quiet;
        bool rv;
        bool verbose;

        void initialize(int* argc, char** argv);
        void handlePrintOpts();
        void parse();

        void printVersion() const;
        void printUsage() const;
        void printResources() const;

        bool getBool(const char* name, bool defaultValue = false);
        void getColor(const char* name, zutty::Color& outColor);
        int getInteger(const char* name, int min, int max);
    };

} // namespace zutty

extern zutty::Options opts;
