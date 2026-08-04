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

#include "options.h"

#include "fatal.h"
#include "toml.h"

#include <std/alg/minmax.h>
#include <std/alg/xchg.h>
#include <std/ios/sys.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/mem/obj_pool.h>
#include <std/sym/s_map.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdlib.h>

using namespace stl;

extern "C" char** environ;

namespace {

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

    static const OptionDesc optionsTable[] = {

        {"altScroll", OptionKind::NoArg, "true", "false", "Alternate scroll mode"},
        {"autoCopy", OptionKind::NoArg, "true", "false", "Sync primary to clipboard"},
        {"bg", OptionKind::SepArg, nullptr, "#000", "Background color"},
        {"boldColors", OptionKind::NoArg, "true", "true", "Enable bright for bold"},
        {"border", OptionKind::SepArg, nullptr, "2", "Border width in pixels"},
        {"config", OptionKind::SepArg, nullptr, nullptr, "Path to the TOML config file"},
        {"cr", OptionKind::SepArg, nullptr, nullptr, "Cursor color"},
        {"dump", OptionKind::SepArg, nullptr, nullptr, "Dump raw PTY input to file"},
        {"fg", OptionKind::SepArg, nullptr, "#fff", "Foreground color"},
        {"font", OptionKind::SepArg, nullptr, "monospace", "Font to use; repeat for fallbacks"},
        {"fontsize", OptionKind::SepArg, nullptr, "16", "Font size"},
        {"geometry", OptionKind::SepArg, nullptr, "80x24", "Terminal size in chars"},
        {"kittyCtrlBaseLayout", OptionKind::NoArg, "true", "false", "Report the ASCII base key as the Kitty primary under Ctrl"},
        {"vulkanInfo", OptionKind::NoArg, "true", "false", "Print Vulkan information"},
        {"help", OptionKind::NoArg, "true", "false", "Print usage listing and quit"},
        {"listres", OptionKind::NoArg, "true", "false", "Print advanced option listing and quit"},
        {"login", OptionKind::NoArg, "true", "false", "Start shell as a login shell"},
        {"no-decorations", OptionKind::NoArg, "true", "false", "Disable window decorations"},
        {"remap", OptionKind::SepArg, nullptr, nullptr, "Rewrite a key chord, from=to; repeat for more"},
        {"rv", OptionKind::NoArg, "true", "false", "Reverse video"},
        {"saveLines", OptionKind::SepArg, nullptr, "500", "Lines of scrollback history"},
        {"shell", OptionKind::SepArg, nullptr, nullptr, "Shell program to run"},
        {"showWraps", OptionKind::NoArg, "true", "false", "Show wrap marks at right margin"},
        {"title", OptionKind::SepArg, nullptr, "Shitty", "Window title"},
        {"verbose", OptionKind::NoArg, "true", "false", "Output info messages"},
        {"version", OptionKind::NoArg, "true", "false", "Print version and quit"},
        {"e", OptionKind::SkipLine, nullptr, nullptr, "Command line to run"},
    };

    static const ResourceDesc resourceTable[] = {

        {"altSendsEscape", "true", "Encode Alt key as ESC prefix"},
        {"modifyOtherKeys", "1", "Key modifier encoding level; 0..2"},
        {"allowOsc52Read", "false", "Allow applications to read clipboard via OSC 52"},
        {"allowWindowOps", "false", "Allow applications to manipulate and query the window"},
        {"osc52Select", "primary", "Selection used by OSC 52 selector s: primary or clipboard"},
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

    // A list of owned strings: NUL-terminated bytes in one arena, one
    // offset per entry. Vector cannot hold non-trivial elements, so the
    // strings never live in it directly.
    struct StringList {
        Buffer arena;
        Vector<u32> offsets;

        void clear();
        void push(StringView value);
        size_t count() const;
        bool empty() const;
        const char* at(size_t index) const;
        void copyFrom(const StringList& other);
    };

    // Option values by name hash: the value bytes are stored NUL
    // terminated, so a lookup hands out a plain C string.
    struct NamedValues {
        ObjPool::Ref pool;
        SymbolMap<Buffer> map;

        NamedValues();

        void put(StringView name, StringView value);
        const char* find(StringView name);
    };

    static StringList configFonts;
    static StringList configRemaps;
    static StringList fontArguments;
    static StringList remapArguments;
    static Vector<const char*> fontPointers;
    static Vector<const char*> remapPointers;
}

void StringList::clear() {
    arena.reset();
    offsets.clear();
}

void StringList::push(StringView value) {
    offsets.pushBack((u32)(arena.used()));
    arena.append(value.data(), value.length());
    arena.append("", 1);
}

size_t StringList::count() const {
    return offsets.length();
}

bool StringList::empty() const {
    return offsets.empty();
}

const char* StringList::at(size_t index) const {
    return (const char*)(arena.data()) + offsets[index];
}

void StringList::copyFrom(const StringList& other) {
    Buffer bytes(other.arena);
    Vector<u32> where(other.offsets);
    arena.xchg(bytes);
    offsets.xchg(where);
}

NamedValues::NamedValues()
    : pool(ObjPool::fromMemory())
    , map(pool.mutPtr())
{
}

void NamedValues::put(StringView name, StringView value) {
    Buffer bytes(value);
    bytes.append("", 1);
    map.insert(name, move(bytes));
}

const char* NamedValues::find(StringView name) {
    Buffer* const value = map.find(name);
    if (value == nullptr) {
        return nullptr;
    }
    return (const char*)(value->data());
}

namespace {

    static NamedValues commandLine;
    static NamedValues configFile;

    // The two list-shaped options; everything else in the config file is a
    // scalar.
    static StringList* configList(StringView name) {
        if (name == StringView(u8"font")) {
            return &configFonts;
        }
        if (name == StringView(u8"remap")) {
            return &configRemaps;
        }
        return nullptr;
    }

    static void writeSpaces(ZeroCopyOutput& output, size_t count) {
        static constexpr u8 spaces[] = u8"                                ";
        while (count != 0) {
            const size_t chunk = count < sizeof(spaces) - 1 ? count : sizeof(spaces) - 1;
            output.write(spaces, chunk);
            count -= chunk;
        }
    }

    static const OptionDesc* findOption(const char* prefix) {
        if (strcmp(prefix, "v") == 0) {
            prefix = "version";
        }

        const OptionDesc* found = nullptr;
        const size_t n = strlen(prefix);

        for (const auto& option : optionsTable) {
            if (strncmp(option.option, prefix, n) != 0) {
                continue;
            }

            if (strlen(option.option) == n) {
                return &option;
            }
            if (found != nullptr) {
                raiseError(StringView(u8"ambiguous option: "), StringView(prefix));
            }
            found = &option;
        }
        return found;
    }

    static bool isAdvancedOption(const char* name) {
        for (const auto& resource : resourceTable) {
            if (strcmp(resource.resource, name) == 0) {
                return true;
            }
        }
        return false;
    }

    // Options that only make sense on a command line stay out of the file.
    static bool isConfigurableOption(StringView name) {
        static const char* const rejected[] = {"help", "version", "listres", "e", "config", "vulkanInfo"};
        for (const char* meta : rejected) {
            if (name == StringView(meta)) {
                return false;
            }
        }
        for (const auto& option : optionsTable) {
            if (name == StringView(option.option)) {
                return true;
            }
        }
        for (const auto& resource : resourceTable) {
            if (name == StringView(resource.resource)) {
                return true;
            }
        }
        return false;
    }

    // Fills configFile/configFonts from the SAX events of one TOML
    // document. A config problem must not keep the terminal from starting:
    // everything suspicious is a warning on stderr and the entry is
    // ignored, so no callback ever aborts the parse.
    struct ConfigSink: public TomlSink {
        const char* path;
        Buffer pending;
        StringList* pendingList;
        bool pendingKnown;
        bool skippingTable;
        int arrayDepth;
        int inlineDepth;

        ConfigSink(const char* path);

        bool tomlTable(const stl::StringView* segments, size_t count, bool array) override;
        bool tomlKey(const stl::StringView* segments, size_t count) override;
        bool tomlScalar(TomlType type, stl::StringView text) override;
        bool tomlArrayBegin() override;
        bool tomlArrayEnd() override;
        bool tomlInlineTableBegin() override;
        bool tomlInlineTableEnd() override;
        void tomlError(size_t line, stl::StringView message) override;

        void warn(const char* what, StringView name);
    };
}

ConfigSink::ConfigSink(const char* path)
    : path(path)
    , pendingList(nullptr)
    , pendingKnown(false)
    , skippingTable(false)
    , arrayDepth(0)
    , inlineDepth(0)
{
}

void ConfigSink::warn(const char* what, StringView name) {
    if (name.empty()) {
        fprintf(stderr, "shitty: %s: %s\n", path, what);
    } else {
        fprintf(stderr, "shitty: %s: %s: %.*s\n", path, what, (int)(name.length()), (const char*)(name.data()));
    }
}

bool ConfigSink::tomlTable(const StringView*, size_t, bool) {
    if (!skippingTable) {
        warn("options are plain keys, tables are ignored", StringView());
    }
    skippingTable = true;
    return true;
}

bool ConfigSink::tomlKey(const StringView* segments, size_t count) {
    if (inlineDepth != 0) {
        return true;
    }
    pending.reset();
    pendingKnown = false;
    if (skippingTable) {
        return true;
    }
    if (count != 1) {
        warn("dotted keys are not options", segments[0]);
        return true;
    }
    pending.append(segments[0].data(), segments[0].length());
    pendingKnown = isConfigurableOption(StringView(pending));
    if (!pendingKnown) {
        warn("unknown option", StringView(pending));
    }
    return true;
}

bool ConfigSink::tomlScalar(TomlType type, StringView text) {
    if (inlineDepth != 0) {
        return true;
    }
    if (arrayDepth != 0) {
        if (pendingList == nullptr) {
            return true;
        }
        if (type != TomlType::String) {
            warn("list entries must be strings", text);
            return true;
        }
        pendingList->push(text);
        return true;
    }
    if (!pendingKnown) {
        return true;
    }
    if (StringList* const list = configList(StringView(pending))) {
        list->clear();
        list->push(text);
        return true;
    }
    configFile.put(StringView(pending), text);
    return true;
}

bool ConfigSink::tomlArrayBegin() {
    if (inlineDepth == 0 && arrayDepth == 0) {
        pendingList = pendingKnown ? configList(StringView(pending)) : nullptr;
        if (pendingList != nullptr) {
            pendingList->clear();
        } else if (pendingKnown) {
            warn("this option does not take a list", StringView(pending));
        }
    }
    arrayDepth += 1;
    return true;
}

bool ConfigSink::tomlArrayEnd() {
    arrayDepth -= 1;
    if (arrayDepth == 0) {
        pendingList = nullptr;
    }
    return true;
}

bool ConfigSink::tomlInlineTableBegin() {
    if (inlineDepth == 0 && pendingKnown) {
        warn("no option takes a table", StringView(pending));
        pendingKnown = false;
    }
    inlineDepth += 1;
    return true;
}

bool ConfigSink::tomlInlineTableEnd() {
    inlineDepth -= 1;
    return true;
}

void ConfigSink::tomlError(size_t line, StringView message) {
    fprintf(stderr, "shitty: %s:%zu: %.*s; ignoring the rest of the file\n", path, line, (int)(message.length()), (const char*)(message.data()));
}

namespace {

    // Expands ${NAME} from the process environment anywhere in the config
    // text before parsing. Deliberately simple: one pass over the whole
    // environment, replacing every occurrence of each variable. Appending
    // the substituted value verbatim to the output keeps a
    // self-referential variable from looping forever.
    static void substituteEnvironment(Buffer& text) {
        for (char** entry = environ; *entry != nullptr; ++entry) {
            const char* equals = strchr(*entry, '=');
            if (equals == nullptr || equals == *entry) {
                continue;
            }
            StringBuilder token;
            token << StringView(u8"${") << StringView((const u8*)(*entry), equals - *entry) << StringView(u8"}");
            const StringView needle(token);
            const StringView value(equals + 1);
            const u8* base = (const u8*)(text.data());
            const size_t used = text.used();
            Buffer replaced;
            bool changed = false;
            size_t at = 0;
            while (at + needle.length() <= used) {
                if (memcmp(base + at, needle.data(), needle.length()) == 0) {
                    replaced.append(value.data(), value.length());
                    at += needle.length();
                    changed = true;
                } else {
                    replaced.append(base + at, 1);
                    at += 1;
                }
            }
            if (changed) {
                replaced.append(base + at, used - at);
                text.xchg(replaced);
            }
        }
    }

    static void loadConfigFile() {
        StringBuilder path;
        bool required = false;
        if (const char* chosen = commandLine.find(StringView(u8"config"))) {
            path << StringView(chosen);
            required = true;
        } else {
            const char* xdg = getenv("XDG_CONFIG_HOME");
            if (xdg != nullptr && xdg[0] != '\0') {
                path << StringView(xdg) << StringView(u8"/shitty/shitty.toml");
            } else {
                const char* home = getenv("HOME");
                if (home == nullptr || home[0] == '\0') {
                    return;
                }
                path << StringView(home) << StringView(u8"/.config/shitty/shitty.toml");
            }
        }
        FILE* file = fopen(path.cStr(), "rb");
        if (file == nullptr) {
            if (required) {
                raiseError(StringView(u8"-config: cannot open "), StringView(path));
            }
            return;
        }
        Buffer text;
        char chunk[4096];
        size_t got = 0;
        while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
            text.append(chunk, got);
        }
        fclose(file);
        substituteEnvironment(text);
        ConfigSink sink(path.cStr());
        parseToml(StringView(text), sink);
    }

    static const char* get(const char* name, const char* fallback = nullptr, OptionSource* src = nullptr) {
        auto withSource = [=](const OptionSource source, const char* value) {
            if (src != nullptr) {
                *src = source;
            }
            return value;
        };

        if (const char* parsed = commandLine.find(StringView(name))) {
            return withSource(OptionSource::CmdLine, parsed);
        }

        if (const char* configured = configFile.find(StringView(name))) {
            return withSource(OptionSource::Config, configured);
        }

        for (const auto& option : optionsTable) {
            if (strcmp(option.option, name) == 0 && option.hardDefault != nullptr) {
                return withSource(OptionSource::HardDefault, option.hardDefault);
            }
        }

        for (const auto& resource : resourceTable) {
            if (strcmp(resource.resource, name) == 0 && resource.hardDefault != nullptr) {
                return withSource(OptionSource::HardDefault, resource.hardDefault);
            }
        }

        return withSource(OptionSource::NONE, fallback);
    }

    // The strtol shape of the old stringstream parsing: leading whitespace
    // and a sign pass, trailing whitespace passes, anything else fails.
    static bool parseNumber(const char* text, long& out) {
        char* end = nullptr;
        errno = 0;
        out = strtol(text, &end, 10);
        if (end == text || errno != 0) {
            return false;
        }
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\v' || *end == '\f' || *end == '\r') {
            ++end;
        }
        return *end == '\0';
    }

    static void getBorder(u16& outBorder) {
        long border = 0;
        if (!parseNumber(get("border"), border) || border < 0 || border > 3000) {
            raiseError(StringView(u8"-border: expected unsigned, max. 3000"));
        }
        outBorder = (u16)(border);
    }

    static void getSaveLines(u16& outSaveLines) {
        long lines = 0;
        if (!parseNumber(get("saveLines"), lines) || lines < 0 || lines > 50000) {
            raiseError(StringView(u8"-saveLines: expected unsigned, max. 50000"));
        }
        outSaveLines = (u16)(lines);
    }

    static void getFontsize(u8& outFontsize) {
        const char* option = commandLine.find(StringView(u8"fontsize"));
        if (option == nullptr && (option = getenv("SHITTY_FONT_SIZE")) == nullptr) {
            option = get("fontsize");
        }
        long size = 0;
        if (!parseNumber(option, size) || size < 1 || size > 255) {
            raiseError(StringView(u8"-fontsize/SHITTY_FONT_SIZE: expected integer within 1..255"));
        }
        outFontsize = (u8)(size);
    }

    static void getGeometry(u16& outCols, u16& outRows) {
        const char* option = get("geometry");
        char* end = nullptr;
        errno = 0;
        const long cols = strtol(option, &end, 10);
        bool valid = end != option && errno == 0;
        const char* rest = end;
        while (valid && (*rest == ' ' || *rest == '\t')) {
            ++rest;
        }
        valid = valid && *rest == 'x';
        long rows = 0;
        if (valid) {
            const char* rowText = rest + 1;
            errno = 0;
            rows = strtol(rowText, &end, 10);
            valid = end != rowText && errno == 0;
            while (valid && (*end == ' ' || *end == '\t')) {
                ++end;
            }
            valid = valid && *end == '\0';
        }
        if (!valid || cols < 1 || cols > UINT16_MAX || rows < 1 || rows > UINT16_MAX) {
            raiseError(StringView(u8"-geometry: expected format <COLS>x<ROWS>"));
        }
        outCols = (u16)(cols);
        outRows = (u16)(rows);
    }

    static u8 convHexDigit(const char* name, const char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }

        raiseError(StringView(u8"-"), StringView(name), StringView(u8": illegal hex digit; expected hex RGB color"));
    }

    static void convColor(const char* name, const char* option, Color& outColor) {
        const char* value = option[0] == '#' ? option + 1 : option;
        switch (strlen(value)) {
            case 3:
                outColor.red = 17 * convHexDigit(name, value[0]);
                outColor.green = 17 * convHexDigit(name, value[1]);
                outColor.blue = 17 * convHexDigit(name, value[2]);
                break;
            case 6:
                outColor.red = (convHexDigit(name, value[0]) << 4) + convHexDigit(name, value[1]);
                outColor.green = (convHexDigit(name, value[2]) << 4) + convHexDigit(name, value[3]);
                outColor.blue = (convHexDigit(name, value[4]) << 4) + convHexDigit(name, value[5]);
                break;
            default:
                raiseError(StringView(u8"-"), StringView(name), StringView(u8": expected hex RGB color"));
        }
    }

    [[noreturn]] static void reportStartupError(StringView message) {
        sysO << StringView(u8"Error: ") << message << StringView(u8"!\nTry -help for usage options.") << endL;
        exit(-1);
    }

}

Options opts;

void Options::initialize(int* argc, char** argv) {
    int output = 1;

    for (int input = 1; input < *argc; ++input) {
        const char* argument = argv[input];
        if ((argument[0] != '-' && argument[0] != '+') || argument[1] == '\0') {
            argv[output++] = argv[input];
            continue;
        }

        const bool enabled = argument[0] == '-';
        const char* name = argument + 1;

        if (strcmp(name, "e") == 0) {
            while (input < *argc) {
                argv[output++] = argv[input++];
            }
            break;
        }

        const OptionDesc* option = findOption(name);
        if (option == nullptr) {
            if (!isAdvancedOption(name)) {
                raiseError(StringView(u8"unknown option: "), StringView(argument));
            }

            if (input + 1 >= *argc) {
                raiseError(StringView(argument), StringView(u8": missing value"));
            }
            commandLine.put(StringView(name), StringView(argv[++input]));
            continue;
        }

        switch (option->parseType) {
            case OptionKind::NoArg:
                commandLine.put(StringView(option->option), StringView(enabled ? option->implValue : "false"));
                break;
            case OptionKind::SepArg:
                if (!enabled) {
                    raiseError(StringView(argument), StringView(u8": '+' is invalid here"));
                }
                if (input + 1 >= *argc) {
                    raiseError(StringView(argument), StringView(u8": missing value"));
                }
                commandLine.put(StringView(option->option), StringView(argv[++input]));
                if (strcmp(option->option, "font") == 0) {
                    fontArguments.push(StringView(argv[input]));
                }
                if (strcmp(option->option, "remap") == 0) {
                    remapArguments.push(StringView(argv[input]));
                }
                break;
            case OptionKind::SkipLine:
                break;
        }
    }

    *argc = output;
    argv[output] = nullptr;

    try {
        loadConfigFile();
    } catch (Exception& error) {
        reportStartupError(error.description());
    }
}

bool Options::getBool(const char* name, bool defaultValue) {
    const char* option = get(name);
    if (option == nullptr) {
        return defaultValue;
    }
    if (strcmp(option, "true") == 0) {
        return true;
    }
    if (strcmp(option, "false") == 0) {
        return false;
    }
    raiseError(StringView(u8"-"), StringView(name), StringView(u8": expected true or false"));
}

void Options::getColor(const char* name, Color& outColor) {
    const char* option = get(name);
    if (option == nullptr) {
        raiseError(StringView(u8"-"), StringView(name), StringView(u8": missing value"));
    }
    convColor(name, option, outColor);
}

int Options::getInteger(const char* name, int min, int max) {
    const char* option = get(name);
    if (option == nullptr) {
        return min;
    }

    long result = 0;
    if (!parseNumber(option, result)) {
        raiseError(StringView(u8"-"), StringView(name), StringView(u8": expected integer"));
    }
    return stl::min(stl::max((long)(min), result), (long)(max));
}

void Options::handlePrintOpts() {
    if (getBool("version")) {
        printVersion();
        exit(0);
    }
    if (getBool("help")) {
        printUsage();
        exit(0);
    }
    if (getBool("listres")) {
        printResources();
        exit(0);
    }
}

void Options::parse() {
    handlePrintOpts();
    try {
        getBorder(border);
        getSaveLines(saveLines);
        if (fontArguments.empty()) {
            fontArguments.copyFrom(configFonts);
        }
        if (fontArguments.empty()) {
            fontArguments.push(StringView(get("font")));
        }
        fontPointers.clear();
        for (size_t index = 0; index < fontArguments.count(); ++index) {
            fontPointers.pushBack(fontArguments.at(index));
        }
        fontnames = fontPointers.data();
        fontnameCount = fontPointers.length();
        if (remapArguments.empty()) {
            remapArguments.copyFrom(configRemaps);
        }
        remapPointers.clear();
        for (size_t index = 0; index < remapArguments.count(); ++index) {
            remapPointers.pushBack(remapArguments.at(index));
        }
        remaps = remapPointers.data();
        remapCount = remapPointers.length();
        getFontsize(fontsize);
        getGeometry(nCols, nRows);
        vulkanInfo = getBool("vulkanInfo");
        shell = get("shell", getenv("SHELL"));
        if (shell == nullptr) {
            shell = "bash";
        }
        title = get("title", nullptr, &titleSource);
        dump = get("dump");
        getColor("fg", fg);
        getColor("bg", bg);
        rv = getBool("rv");
        if (rv) {
            xchg(fg, bg);
        }
        if (get("cr") != nullptr) {
            getColor("cr", cr);
        } else {
            cr = fg;
        }
        altScrollMode = getBool("altScroll");
        altSendsEscape = getBool("altSendsEscape");
        autoCopyMode = getBool("autoCopy");
        allowOsc52Read = getBool("allowOsc52Read");
        allowWindowOps = getBool("allowWindowOps");
        const char* osc52Select = get("osc52Select");
        if (strcmp(osc52Select, "primary") != 0 && strcmp(osc52Select, "clipboard") != 0) {
            raiseError(StringView(u8"-osc52Select: expected primary or clipboard"));
        }
        osc52SelectClipboard = strcmp(osc52Select, "clipboard") == 0;
        boldColors = getBool("boldColors");
        kittyCtrlBaseLayout = getBool("kittyCtrlBaseLayout");
        noDecorations = getBool("no-decorations");
        login = getBool("login");
        showWraps = getBool("showWraps");
        verbose = getBool("verbose");
        modifyOtherKeys = getInteger("modifyOtherKeys", 0, 2);
    } catch (Exception& error) {
        reportStartupError(error.description());
    }
}

void Options::printVersion() const {
    sysO << StringView(u8"Shitty " SHITTY_VERSION "\nCopyright (C) 2026 Shitty team") << endL;
}

void Options::printUsage() const {
    printVersion();
    OutBuf output(stdoutStream());
    output << StringView(u8"Usage:\n  st [-option ...] [shell]\n\nOptions:\n");
    size_t maxWidth = 0;
    for (const auto& option : optionsTable) {
        maxWidth = max(maxWidth, strlen(option.option));
    }
    for (const auto& option : optionsTable) {
        output << StringView(u8"  -") << StringView(option.option);
        writeSpaces(output, maxWidth + 3 - strlen(option.option));
        output << StringView(option.helpDescr);
        if (option.hardDefault != nullptr && option.parseType != OptionKind::NoArg) {
            output << StringView(u8" (default: ") << StringView(option.hardDefault) << StringView(u8")");
        }
        output << endL;
    }
    output << endL;
}

void Options::printResources() const {
    printVersion();
    OutBuf output(stdoutStream());
    output << StringView(u8"Advanced options:\n");
    size_t maxWidth = 0;
    for (const auto& resource : resourceTable) {
        maxWidth = max(maxWidth, strlen(resource.resource));
    }
    for (const auto& resource : resourceTable) {
        output << StringView(u8"  -") << StringView(resource.resource);
        writeSpaces(output, maxWidth + 3 - strlen(resource.resource));
        output << StringView(resource.helpDescr);
        if (resource.hardDefault != nullptr) {
            output << StringView(u8" (default: ") << StringView(resource.hardDefault) << StringView(u8")");
        }
        output << endL;
    }
    output << endL;
}
