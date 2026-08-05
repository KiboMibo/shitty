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
#include "terminal_colors.h"
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
        // Options that only make sense on a command line stay out of the
        // config file.
        bool cliOnly = false;
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
        {"boldColors", OptionKind::NoArg, "true", "false", "Brighten bold text's palette colors"},
        {"border", OptionKind::SepArg, nullptr, "2", "Border width in pixels"},
        {"config", OptionKind::SepArg, nullptr, nullptr, "Path to the TOML config file", true},
        {"colorScheme", OptionKind::SepArg, nullptr, nullptr, "Named terminal color scheme"},
        {"cr", OptionKind::SepArg, nullptr, nullptr, "Cursor color"},
        {"dump", OptionKind::SepArg, nullptr, nullptr, "Dump raw PTY input to file"},
        {"fg", OptionKind::SepArg, nullptr, "#fff", "Foreground color"},
        {"font", OptionKind::SepArg, nullptr, "monospace", "Font to use; repeat for fallbacks"},
        {"fontsize", OptionKind::SepArg, nullptr, "16", "Font size"},
        {"geometry", OptionKind::SepArg, nullptr, "80x24", "Terminal size in chars"},
        {"kittyCtrlBaseLayout", OptionKind::NoArg, "true", "false", "Report the ASCII base key as the Kitty primary under Ctrl"},
        {"vulkanInfo", OptionKind::NoArg, "true", "false", "Print Vulkan information", true},
        {"help", OptionKind::NoArg, "true", "false", "Print usage listing and quit", true},
        {"listres", OptionKind::NoArg, "true", "false", "Print advanced option listing and quit", true},
        {"listColorSchemes", OptionKind::NoArg, "true", "false", "Print terminal color scheme names and quit", true},
        {"login", OptionKind::NoArg, "true", "false", "Start shell as a login shell"},
        {"no-decorations", OptionKind::NoArg, "true", "false", "Disable window decorations"},
        {"remap", OptionKind::SepArg, nullptr, nullptr, "Rewrite a key chord, from=to; repeat for more"},
        {"rv", OptionKind::NoArg, "true", "false", "Reverse video"},
        {"saveLines", OptionKind::SepArg, nullptr, "500", "Lines of scrollback history"},
        {"shell", OptionKind::SepArg, nullptr, nullptr, "Shell program to run"},
        {"showWraps", OptionKind::NoArg, "true", "false", "Show wrap marks at right margin"},
        {"title", OptionKind::SepArg, nullptr, "Shitty", "Window title"},
        {"uriScheme", OptionKind::SepArg, nullptr, nullptr, "Open a plain URI with this scheme; repeat for more, default http https file"},
        {"verbose", OptionKind::NoArg, "true", "false", "Output info messages"},
        {"version", OptionKind::NoArg, "true", "false", "Print version and quit", true},
        {"e", OptionKind::SkipLine, nullptr, nullptr, "Command line to run", true},
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

    // Everything the parser stores - scalar values and list entries alike
    // - is interned into the pool the Options instance itself lives in,
    // so the parsed result owns nothing separately and dies with its
    // pool.
    struct OptionsParser final: public Options {
        OptionsParser(ObjPool& owner, char** argv, int argc);

        void initialize(int* argc, char** argv);
        void handlePrintOpts();
        void parse();
        void loadConfigFile();
        bool get(const char* name, StringView& out, OptionSource* src = nullptr);
        const OptionDesc* findOption(const char* prefix);
        bool isAdvancedOption(StringView name) const;
        bool isConfigurableOption(StringView name) const;
        void getBorder(u16& outBorder);
        void getSaveLines(u16& outSaveLines);
        void getFontsize(u8& outFontsize);
        void getGeometry(u16& outCols, u16& outRows);
        void printVersion() const;
        void printUsage() const;
        void printResources() const;
        void printColorSchemes() const;
        bool getBool(const char* name, bool defaultValue = false);
        void getColor(const char* name, Color& outColor);
        int getInteger(const char* name, int min, int max);
        Vector<StringView>* configList(StringView name);

        ObjPool& pool;
        Darts optionTrie;
        Darts resourceTrie;
        SymbolMap<StringView> commandLine;
        SymbolMap<StringView> configFile;
        Vector<StringView> configFonts;
        Vector<StringView> configRemaps;
        Vector<StringView> configUriSchemes;
    };
}

// The list-shaped options; everything else in the config file is a
// scalar.
Vector<StringView>* OptionsParser::configList(StringView name) {
    if (name == StringView(u8"font")) {
        return &configFonts;
    }
    if (name == StringView(u8"remap")) {
        return &configRemaps;
    }
    if (name == StringView(u8"uriScheme")) {
        return &configUriSchemes;
    }
    return nullptr;
}

namespace {

    static void writeSpaces(ZeroCopyOutput& output, size_t count) {
        static constexpr u8 spaces[] = u8"                                ";
        while (count != 0) {
            const size_t chunk = count < sizeof(spaces) - 1 ? count : sizeof(spaces) - 1;
            output.write(spaces, chunk);
            count -= chunk;
        }
    }
}

const OptionDesc* OptionsParser::findOption(const char* prefix) {
    if (strcmp(prefix, "v") == 0) {
        prefix = "version";
    }

    const i32 resolved = optionTrie.resolve(StringView(prefix));
    if (resolved == Darts::ambiguous) {
        raiseError(StringView(u8"ambiguous option: "), StringView(prefix));
    }
    if (resolved < 0) {
        return nullptr;
    }
    return &optionsTable[resolved];
}

bool OptionsParser::isAdvancedOption(StringView name) const {
    return resourceTrie.find(name) >= 0;
}

bool OptionsParser::isConfigurableOption(StringView name) const {
    const i32 option = optionTrie.find(name);
    if (option >= 0) {
        return !optionsTable[option].cliOnly;
    }
    return isAdvancedOption(name);
}

namespace {

    // Fills configFile/configFonts from the SAX events of one TOML
    // document. A config problem must not keep the terminal from starting:
    // everything suspicious is a warning on stderr and the entry is
    // ignored, so no callback ever aborts the parse.
    struct ConfigSink: public TomlSink {
        OptionsParser& options;
        const char* path;
        Buffer pending;
        Vector<StringView>* pendingList;
        bool pendingKnown;
        bool skippingTable;
        int arrayDepth;
        int inlineDepth;

        ConfigSink(OptionsParser& options, const char* path);

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

ConfigSink::ConfigSink(OptionsParser& options_, const char* path)
    : options(options_)
    , path(path)
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
    pendingKnown = options.isConfigurableOption(StringView(pending));
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
        pendingList->pushBack(options.pool.intern(text));
        return true;
    }
    if (!pendingKnown) {
        return true;
    }
    if (Vector<StringView>* const list = options.configList(StringView(pending))) {
        list->clear();
        list->pushBack(options.pool.intern(text));
        return true;
    }
    options.configFile.insert(StringView(pending), options.pool.intern(text));
    return true;
}

bool ConfigSink::tomlArrayBegin() {
    if (inlineDepth == 0 && arrayDepth == 0) {
        pendingList = pendingKnown ? options.configList(StringView(pending)) : nullptr;
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
}

void OptionsParser::loadConfigFile() {
    StringBuilder path;
    bool required = false;
    if (const StringView* chosen = commandLine.find(StringView(u8"config"))) {
        path << *chosen;
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
    ConfigSink sink(*this, path.cStr());
    parseToml(StringView(text), sink);
}

bool OptionsParser::get(const char* name, StringView& out, OptionSource* src) {
    const auto withSource = [&](const OptionSource source, StringView value) {
        if (src != nullptr) {
            *src = source;
        }
        out = value;
        return source != OptionSource::NONE;
    };

    if (const StringView* parsed = commandLine.find(StringView(name))) {
        return withSource(OptionSource::CmdLine, *parsed);
    }

    if (const StringView* configured = configFile.find(StringView(name))) {
        return withSource(OptionSource::Config, *configured);
    }

    const i32 option = optionTrie.find(StringView(name));
    if (option >= 0 && optionsTable[option].hardDefault != nullptr) {
        return withSource(OptionSource::HardDefault, StringView(optionsTable[option].hardDefault));
    }

    const i32 resource = resourceTrie.find(StringView(name));
    if (resource >= 0 && resourceTable[resource].hardDefault != nullptr) {
        return withSource(OptionSource::HardDefault, StringView(resourceTable[resource].hardDefault));
    }

    return withSource(OptionSource::NONE, StringView());
}

namespace {

    // The strtol shape of the old stringstream parsing: leading whitespace
    // and a sign pass, trailing whitespace passes, anything else fails.
    // Every stored value is NUL terminated in the pool, so data() is a
    // valid C string.
    static bool parseNumber(StringView text, long& out) {
        const char* begin = (const char*)(text.data());
        if (begin == nullptr) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        out = strtol(begin, &end, 10);
        if (end == begin || errno != 0) {
            return false;
        }
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\v' || *end == '\f' || *end == '\r') {
            ++end;
        }
        return *end == '\0';
    }
}

void OptionsParser::getBorder(u16& outBorder) {
    StringView value;
    long border = 0;
    if (!get("border", value) || !parseNumber(value, border) || border < 0 || border > 3000) {
        raiseError(StringView(u8"-border: expected unsigned, max. 3000"));
    }
    outBorder = (u16)(border);
}

void OptionsParser::getSaveLines(u16& outSaveLines) {
    StringView value;
    long lines = 0;
    if (!get("saveLines", value) || !parseNumber(value, lines) || lines < 0 || lines > 50000) {
        raiseError(StringView(u8"-saveLines: expected unsigned, max. 50000"));
    }
    outSaveLines = (u16)(lines);
}

void OptionsParser::getFontsize(u8& outFontsize) {
    StringView value;
    if (const StringView* argument = commandLine.find(StringView(u8"fontsize"))) {
        value = *argument;
    } else if (const char* env = getenv("SHITTY_FONT_SIZE")) {
        value = StringView(env);
    } else {
        get("fontsize", value);
    }
    long size = 0;
    if (!parseNumber(value, size) || size < 1 || size > 255) {
        raiseError(StringView(u8"-fontsize/SHITTY_FONT_SIZE: expected integer within 1..255"));
    }
    outFontsize = (u8)(size);
}

void OptionsParser::getGeometry(u16& outCols, u16& outRows) {
    StringView value;
    get("geometry", value);
    const char* option = (const char*)(value.data());
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

namespace {

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

    static void convColor(const char* name, StringView option, Color& outColor) {
        const char* value = (const char*)(option.data());
        size_t length = option.length();
        if (length != 0 && value[0] == '#') {
            ++value;
            --length;
        }
        switch (length) {
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

OptionsParser::OptionsParser(ObjPool& owner, char** argv, int argc)
    : pool(owner)
    , commandLine(&owner)
    , configFile(&owner)
{
    {
        Vector<StringView> names;
        for (const auto& option : optionsTable) {
            names.pushBack(StringView(option.option));
        }
        optionTrie.build(names.data(), names.length());
        names.clear();
        for (const auto& resource : resourceTable) {
            names.pushBack(StringView(resource.resource));
        }
        resourceTrie.build(names.data(), names.length());
    }
    initialize(&argc, argv);
    parse();
    if (verbose) {
        printVersion();
    }
}

Options* Options::create(ObjPool& pool, char** argv, int argc) {
    return pool.make<OptionsParser>(pool, argv, argc);
}

bool Options::uriSchemeAllowed(StringView scheme) const {
    u8 folded[128];
    if (scheme.length() > sizeof(folded)) {
        return false;
    }
    for (size_t index = 0; index < scheme.length(); ++index) {
        const u8 byte = scheme[index];
        folded[index] = byte >= 'A' && byte <= 'Z' ? (u8)(byte + ('a' - 'A')) : byte;
    }
    return uriSchemeTrie.find(StringView(folded, scheme.length())) != Darts::missing;
}

void OptionsParser::initialize(int* argc, char** argv) {
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
            if (!isAdvancedOption(StringView(name))) {
                raiseError(StringView(u8"unknown option: "), StringView(argument));
            }

            if (input + 1 >= *argc) {
                raiseError(StringView(argument), StringView(u8": missing value"));
            }
            commandLine.insert(StringView(name), pool.intern(StringView(argv[++input])));
            continue;
        }

        switch (option->parseType) {
            case OptionKind::NoArg:
                commandLine.insert(StringView(option->option), StringView(enabled ? option->implValue : "false"));
                break;
            case OptionKind::SepArg: {
                if (!enabled) {
                    raiseError(StringView(argument), StringView(u8": '+' is invalid here"));
                }
                if (input + 1 >= *argc) {
                    raiseError(StringView(argument), StringView(u8": missing value"));
                }
                const StringView value = pool.intern(StringView(argv[++input]));
                commandLine.insert(StringView(option->option), value);
                if (strcmp(option->option, "font") == 0) {
                    fontnames.pushBack(value);
                }
                if (strcmp(option->option, "remap") == 0) {
                    remaps.pushBack(value);
                }
                if (strcmp(option->option, "uriScheme") == 0) {
                    uriSchemes.pushBack(value);
                }
                break;
            }
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

bool OptionsParser::getBool(const char* name, bool defaultValue) {
    StringView option;
    if (!get(name, option)) {
        return defaultValue;
    }
    if (option == StringView(u8"true")) {
        return true;
    }
    if (option == StringView(u8"false")) {
        return false;
    }
    raiseError(StringView(u8"-"), StringView(name), StringView(u8": expected true or false"));
}

void OptionsParser::getColor(const char* name, Color& outColor) {
    StringView option;
    if (!get(name, option)) {
        raiseError(StringView(u8"-"), StringView(name), StringView(u8": missing value"));
    }
    convColor(name, option, outColor);
}

int OptionsParser::getInteger(const char* name, int min, int max) {
    StringView option;
    if (!get(name, option)) {
        return min;
    }

    long result = 0;
    if (!parseNumber(option, result)) {
        raiseError(StringView(u8"-"), StringView(name), StringView(u8": expected integer"));
    }
    return stl::min(stl::max((long)(min), result), (long)(max));
}

void OptionsParser::handlePrintOpts() {
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
    if (getBool("listColorSchemes")) {
        printColorSchemes();
        exit(0);
    }
}

void OptionsParser::parse() {
    handlePrintOpts();
    try {
        getBorder(border);
        getSaveLines(saveLines);
        if (fontnames.empty()) {
            fontnames.append(configFonts.data(), configFonts.length());
        }
        if (fontnames.empty()) {
            StringView fallback;
            get("font", fallback);
            fontnames.pushBack(fallback);
        }
        if (remaps.empty()) {
            remaps.append(configRemaps.data(), configRemaps.length());
        }
        if (uriSchemes.empty()) {
            uriSchemes.append(configUriSchemes.data(), configUriSchemes.length());
        }
        if (uriSchemes.empty()) {
            // The conservative default: schemes with a handler on any sane
            // desktop. A configured list replaces this outright.
            uriSchemes.pushBack(StringView(u8"http"));
            uriSchemes.pushBack(StringView(u8"https"));
            uriSchemes.pushBack(StringView(u8"file"));
        }
        {
            // The trie is queried with a lowercased probe, so fold the
            // configured spellings once here.
            Vector<StringView> folded;
            for (const StringView scheme : uriSchemes) {
                u8* bytes = (u8*)(pool.allocate(scheme.length() + 1));
                for (size_t index = 0; index < scheme.length(); ++index) {
                    const u8 byte = scheme[index];
                    bytes[index] = byte >= 'A' && byte <= 'Z' ? (u8)(byte + ('a' - 'A')) : byte;
                }
                bytes[scheme.length()] = '\0';
                folded.pushBack(StringView(bytes, scheme.length()));
            }
            uriSchemeTrie.build(folded.data(), folded.length());
        }
        getFontsize(fontsize);
        getGeometry(nCols, nRows);
        vulkanInfo = getBool("vulkanInfo");
        if (!get("shell", shell)) {
            if (const char* env = getenv("SHELL")) {
                shell = pool.intern(StringView(env));
            }
        }
        if (shell.empty()) {
            shell = StringView(u8"bash");
        }
        get("title", title, &titleSource);
        get("dump", dump);
        OptionSource schemeSource = OptionSource::NONE;
        const TerminalColorScheme* scheme = nullptr;
        StringView schemeName;
        if (get("colorScheme", schemeName, &schemeSource)) {
            scheme = TerminalColorScheme::find(schemeName);
            if (scheme == nullptr) {
                raiseError(StringView(u8"-colorScheme: unknown scheme: "), schemeName, StringView(u8"; use -listColorSchemes"));
            }
            fg = scheme->foregroundColor();
            bg = scheme->backgroundColor();
            palette = scheme->ansiPalette();
        }
        auto applyColorOption = [&](const char* name, Color& color) {
            OptionSource source = OptionSource::NONE;
            StringView value;
            get(name, value, &source);
            if (scheme == nullptr || (int)(source) >= (int)(schemeSource)) {
                getColor(name, color);
            }
        };
        applyColorOption("fg", fg);
        applyColorOption("bg", bg);
        static const char* const paletteNames[] = {
            "color0",
            "color1",
            "color2",
            "color3",
            "color4",
            "color5",
            "color6",
            "color7",
            "color8",
            "color9",
            "color10",
            "color11",
            "color12",
            "color13",
            "color14",
            "color15",
        };
        for (size_t index = 0; index < 16; ++index) {
            applyColorOption(paletteNames[index], palette[index]);
        }
        rv = getBool("rv");
        if (rv) {
            xchg(fg, bg);
        }
        StringView cursor;
        if (get("cr", cursor)) {
            convColor("cr", cursor, cr);
        } else {
            cr = fg;
        }
        altScrollMode = getBool("altScroll");
        altSendsEscape = getBool("altSendsEscape");
        autoCopyMode = getBool("autoCopy");
        allowOsc52Read = getBool("allowOsc52Read");
        allowWindowOps = getBool("allowWindowOps");
        StringView osc52Select;
        get("osc52Select", osc52Select);
        if (osc52Select != StringView(u8"primary") && osc52Select != StringView(u8"clipboard")) {
            raiseError(StringView(u8"-osc52Select: expected primary or clipboard"));
        }
        osc52SelectClipboard = osc52Select == StringView(u8"clipboard");
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

void OptionsParser::printVersion() const {
    sysO << StringView(u8"Shitty " SHITTY_VERSION "\nCopyright (C) 2026 Shitty team") << endL;
}

void OptionsParser::printUsage() const {
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

void OptionsParser::printResources() const {
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

void OptionsParser::printColorSchemes() const {
    OutBuf output(stdoutStream());
    for (size_t index = 0; index < TerminalColorScheme::count(); ++index) {
        output << StringView(TerminalColorScheme::all()[index].name) << endL;
    }
}
