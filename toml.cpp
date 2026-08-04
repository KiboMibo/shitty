/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "toml.h"

#include <std/lib/buffer.h>

#include <cstring>

using namespace stl;

namespace {
    u32 hexValue(u8 byte) noexcept;

    struct TomlKeySegment {
        u32 offset;
        u32 length;
    };

    // The ragel scanner in toml.rl delivers tokens; this driver is the
    // pushdown that enforces the line and container structure of TOML and
    // turns the token stream into TomlSink events.
    struct TomlParser {
        static constexpr size_t maxDepth = 64;
        static constexpr size_t maxKeySegments = 64;

        enum class Punct : u8 {
            BracketOpen,
            BracketClose,
            BraceOpen,
            BraceClose,
            Equals,
            Comma,
            Dot,
        };

        enum class Quotes : u8 {
            Basic,
            Literal,
            MlBasic,
            MlLiteral,
        };

        enum class Mode : u8 {
            LineStart,
            HeaderIntro,
            HeaderKey,
            HeaderDot,
            HeaderSecondClose,
            HeaderDone,
            Key,
            KeyDot,
            Value,
            LineEnd,
            ArrayNext,
            InlineStart,
            InlineNext,
        };

        enum class Context : u8 {
            Array,
            Inline,
        };

        TomlSink& sink;
        int cs;
        int act;
        const u8* ts;
        const u8* te;
        size_t line;
        bool failed;
        bool aborted;
        bool headerArray;
        const char* message;
        Mode mode;
        const u8* bracketEnd;
        Context stack[maxDepth];
        size_t depth;
        Buffer keyBuf;
        Buffer valBuf;
        TomlKeySegment segments[maxKeySegments];
        size_t segmentCount;
        u32 lastSegmentEnd;

        TomlParser(TomlSink& sink);

        bool onNewline();
        bool onPunct(Punct punct, const u8* begin, const u8* end);
        bool onScalar(const u8* begin, const u8* end);
        bool onBareWord(const u8* begin, const u8* end);
        bool onString(const u8* begin, const u8* end, Quotes quotes);

    private:
        bool fail(const char* what);
        bool expectingKey() const;
        Mode keyDoneMode() const;
        void afterValue();
        bool endKeySegment();
        bool wordSegments(const u8* begin, const u8* end);
        bool emitKey();
        bool emitTable();
        bool emitScalarValue();
        bool emitStringValue();
        bool evArrayBegin();
        bool evArrayEnd();
        bool evInlineBegin();
        bool evInlineEnd();
        bool pushCodepoint(Buffer& target, u32 value);
        bool decodeBasic(const u8* begin, const u8* end, Buffer& target);
        void decodeLiteral(const u8* begin, const u8* end, Buffer& target);
        bool collectSegments(StringView* views);
        TomlType classifyScalar() const;
        bool validDate() const;
    };
}

TomlParser::TomlParser(TomlSink& sink)
    : sink(sink)
    , cs(0)
    , act(0)
    , ts(nullptr)
    , te(nullptr)
    , line(1)
    , failed(false)
    , aborted(false)
    , headerArray(false)
    , message("syntax error")
    , mode(Mode::LineStart)
    , bracketEnd(nullptr)
    , depth(0)
    , segmentCount(0)
    , lastSegmentEnd(0)
{
}

bool TomlParser::fail(const char* what) {
    failed = true;
    message = what;
    return false;
}

bool TomlParser::expectingKey() const {
    switch (mode) {
        case Mode::LineStart:
        case Mode::HeaderIntro:
        case Mode::HeaderKey:
        case Mode::Key:
        case Mode::InlineStart:
            return true;
        default:
            return false;
    }
}

TomlParser::Mode TomlParser::keyDoneMode() const {
    if (mode == Mode::HeaderIntro || mode == Mode::HeaderKey) {
        return Mode::HeaderDot;
    }
    return Mode::KeyDot;
}

void TomlParser::afterValue() {
    if (depth == 0) {
        mode = Mode::LineEnd;
    } else if (stack[depth - 1] == Context::Array) {
        mode = Mode::ArrayNext;
    } else {
        mode = Mode::InlineNext;
    }
}

bool TomlParser::onNewline() {
    line += 1;
    switch (mode) {
        case Mode::LineStart:
            return true;
        case Mode::LineEnd:
        case Mode::HeaderDone:
            mode = Mode::LineStart;
            return true;
        case Mode::Value:
        case Mode::ArrayNext:
            if (depth != 0 && stack[depth - 1] == Context::Array) {
                return true;
            }
            return fail("line breaks off in the middle of a value");
        default:
            return fail("unexpected end of line");
    }
}

bool TomlParser::onPunct(Punct punct, const u8* begin, const u8* end) {
    switch (punct) {
        case Punct::BracketOpen:
            if (mode == Mode::LineStart) {
                mode = Mode::HeaderIntro;
                headerArray = false;
                bracketEnd = end;
                return true;
            }
            if (mode == Mode::HeaderIntro && segmentCount == 0 && begin == bracketEnd) {
                // The doubled brackets of an array-of-tables header must be
                // adjacent; "[ [" opens a table named by a group that never
                // parses.
                headerArray = true;
                mode = Mode::HeaderKey;
                return true;
            }
            if (mode == Mode::Value) {
                if (depth == maxDepth) {
                    return fail("nesting too deep");
                }
                if (!evArrayBegin()) {
                    return false;
                }
                stack[depth++] = Context::Array;
                return true;
            }
            return fail("unexpected '['");
        case Punct::BracketClose:
            if (mode == Mode::HeaderDot && segmentCount != 0) {
                bracketEnd = end;
                return emitTable();
            }
            if (mode == Mode::HeaderSecondClose && begin == bracketEnd) {
                mode = Mode::HeaderDone;
                return true;
            }
            if ((mode == Mode::Value || mode == Mode::ArrayNext) && depth != 0 && stack[depth - 1] == Context::Array) {
                depth -= 1;
                if (!evArrayEnd()) {
                    return false;
                }
                afterValue();
                return true;
            }
            return fail("unexpected ']'");
        case Punct::BraceOpen:
            if (mode == Mode::Value) {
                if (depth == maxDepth) {
                    return fail("nesting too deep");
                }
                if (!evInlineBegin()) {
                    return false;
                }
                stack[depth++] = Context::Inline;
                mode = Mode::InlineStart;
                return true;
            }
            return fail("unexpected '{'");
        case Punct::BraceClose:
            if ((mode == Mode::InlineStart || mode == Mode::InlineNext) && depth != 0 && stack[depth - 1] == Context::Inline) {
                depth -= 1;
                if (!evInlineEnd()) {
                    return false;
                }
                afterValue();
                return true;
            }
            return fail("unexpected '}'");
        case Punct::Equals:
            if (mode == Mode::KeyDot && segmentCount != 0) {
                if (!emitKey()) {
                    return false;
                }
                mode = Mode::Value;
                return true;
            }
            return fail("unexpected '='");
        case Punct::Comma:
            if (mode == Mode::ArrayNext) {
                mode = Mode::Value;
                return true;
            }
            if (mode == Mode::InlineNext) {
                mode = Mode::Key;
                return true;
            }
            return fail("unexpected ','");
        case Punct::Dot:
            if (mode == Mode::KeyDot) {
                mode = Mode::Key;
                return true;
            }
            if (mode == Mode::HeaderDot) {
                mode = Mode::HeaderKey;
                return true;
            }
            return fail("unexpected '.'");
    }
    return fail("unexpected token");
}

bool TomlParser::endKeySegment() {
    if (segmentCount == maxKeySegments) {
        return fail("too many key segments");
    }
    const u32 used = (u32)keyBuf.used();
    segments[segmentCount].offset = lastSegmentEnd;
    segments[segmentCount].length = used - lastSegmentEnd;
    segmentCount += 1;
    lastSegmentEnd = used;
    return true;
}

// A scalar-looking token in key position contributes its raw spelling,
// split at dots: "3.14" is the dotted key 3.14 and "1979-05-27" is one
// bare key. Anything outside the bare key alphabet cannot be a key.
bool TomlParser::wordSegments(const u8* begin, const u8* end) {
    const u8* segment = begin;
    for (const u8* at = begin; at != end; ++at) {
        const u8 byte = *at;
        if (byte == '.') {
            keyBuf.append(segment, at - segment);
            if (!endKeySegment()) {
                return false;
            }
            segment = at + 1;
            continue;
        }
        const bool bare = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-';
        if (!bare) {
            return fail("character not allowed in a key");
        }
    }
    keyBuf.append(segment, end - segment);
    return endKeySegment();
}

bool TomlParser::onScalar(const u8* begin, const u8* end) {
    if (expectingKey()) {
        const Mode done = keyDoneMode();
        if (!wordSegments(begin, end)) {
            return false;
        }
        mode = done;
        return true;
    }
    if (mode != Mode::Value) {
        return fail("value in a wrong place");
    }
    valBuf.reset();
    for (const u8* at = begin; at != end; ++at) {
        if (*at != '_') {
            valBuf.append(at, 1);
        }
    }
    return emitScalarValue();
}

bool TomlParser::onBareWord(const u8* begin, const u8* end) {
    if (expectingKey()) {
        const Mode done = keyDoneMode();
        if (!wordSegments(begin, end)) {
            return false;
        }
        mode = done;
        return true;
    }
    return fail("bare word is not a value");
}

bool TomlParser::onString(const u8* begin, const u8* end, Quotes quotes) {
    const bool multiline = quotes == Quotes::MlBasic || quotes == Quotes::MlLiteral;
    for (const u8* at = begin; at != end; ++at) {
        if (*at == '\n') {
            line += 1;
        }
    }
    if (expectingKey()) {
        if (multiline) {
            return fail("multiline strings cannot be keys");
        }
        const Mode done = keyDoneMode();
        if (quotes == Quotes::Basic) {
            if (!decodeBasic(begin + 1, end - 1, keyBuf)) {
                return false;
            }
        } else {
            decodeLiteral(begin + 1, end - 1, keyBuf);
        }
        if (!endKeySegment()) {
            return false;
        }
        mode = done;
        return true;
    }
    if (mode != Mode::Value) {
        return fail("value in a wrong place");
    }
    const u8* inner = begin + (multiline ? 3 : 1);
    const u8* innerEnd = end - (multiline ? 3 : 1);
    if (multiline) {
        // A newline right after the opening delimiter is trimmed.
        if (inner != innerEnd && *inner == '\r') {
            inner += 1;
        }
        if (inner != innerEnd && *inner == '\n') {
            inner += 1;
        }
    }
    valBuf.reset();
    if (quotes == Quotes::Basic || quotes == Quotes::MlBasic) {
        if (!decodeBasic(inner, innerEnd, valBuf)) {
            return false;
        }
    } else {
        decodeLiteral(inner, innerEnd, valBuf);
    }
    return emitStringValue();
}

bool TomlParser::pushCodepoint(Buffer& target, u32 value) {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return fail("escape is not a Unicode scalar value");
    }
    u8 bytes[4];
    size_t count = 0;
    if (value < 0x80) {
        bytes[count++] = (u8)value;
    } else if (value < 0x800) {
        bytes[count++] = (u8)(0xc0 | (value >> 6));
        bytes[count++] = (u8)(0x80 | (value & 0x3f));
    } else if (value < 0x10000) {
        bytes[count++] = (u8)(0xe0 | (value >> 12));
        bytes[count++] = (u8)(0x80 | ((value >> 6) & 0x3f));
        bytes[count++] = (u8)(0x80 | (value & 0x3f));
    } else {
        bytes[count++] = (u8)(0xf0 | (value >> 18));
        bytes[count++] = (u8)(0x80 | ((value >> 12) & 0x3f));
        bytes[count++] = (u8)(0x80 | ((value >> 6) & 0x3f));
        bytes[count++] = (u8)(0x80 | (value & 0x3f));
    }
    target.append(bytes, count);
    return true;
}

// The scanner has already validated the escape syntax; this pass only
// decodes it. A backslash before whitespace or a newline is the
// line-ending backslash, which swallows everything up to the next
// content byte.
bool TomlParser::decodeBasic(const u8* begin, const u8* end, Buffer& target) {
    const u8* at = begin;
    while (at != end) {
        const u8 byte = *at;
        if (byte != '\\') {
            target.append(at, 1);
            at += 1;
            continue;
        }
        at += 1;
        const u8 kind = *at;
        if (kind == 'u' || kind == 'U') {
            const size_t digits = kind == 'u' ? 4 : 8;
            u32 value = 0;
            for (size_t i = 1; i <= digits; ++i) {
                value = value * 16 + hexValue(at[i]);
            }
            if (!pushCodepoint(target, value)) {
                return false;
            }
            at += 1 + digits;
            continue;
        }
        if (kind == ' ' || kind == '\t' || kind == '\r' || kind == '\n') {
            while (at != end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')) {
                at += 1;
            }
            continue;
        }
        u8 decoded = kind;
        if (kind == 'b') {
            decoded = '\b';
        } else if (kind == 'f') {
            decoded = '\f';
        } else if (kind == 'n') {
            decoded = '\n';
        } else if (kind == 'r') {
            decoded = '\r';
        } else if (kind == 't') {
            decoded = '\t';
        }
        target.append(&decoded, 1);
        at += 1;
    }
    return true;
}

void TomlParser::decodeLiteral(const u8* begin, const u8* end, Buffer& target) {
    target.append(begin, end - begin);
}

bool TomlParser::collectSegments(StringView* views) {
    const u8* base = (const u8*)keyBuf.data();
    for (size_t i = 0; i < segmentCount; ++i) {
        views[i] = StringView(base + segments[i].offset, (size_t)segments[i].length);
    }
    return segmentCount != 0;
}

bool TomlParser::emitKey() {
    StringView views[maxKeySegments];
    if (!collectSegments(views)) {
        return fail("empty key");
    }
    const bool keep = sink.tomlKey(views, segmentCount);
    segmentCount = 0;
    lastSegmentEnd = 0;
    keyBuf.reset();
    if (!keep) {
        aborted = true;
    }
    return keep;
}

bool TomlParser::emitTable() {
    StringView views[maxKeySegments];
    if (!collectSegments(views)) {
        return fail("empty table name");
    }
    const bool keep = sink.tomlTable(views, segmentCount, headerArray);
    segmentCount = 0;
    lastSegmentEnd = 0;
    keyBuf.reset();
    if (!keep) {
        aborted = true;
        return false;
    }
    mode = headerArray ? Mode::HeaderSecondClose : Mode::HeaderDone;
    return true;
}

TomlType TomlParser::classifyScalar() const {
    // The scanner has already validated the token, so lightweight checks
    // are enough to tell the alternatives of the scalar union apart.
    const u8* text = (const u8*)valBuf.data();
    const size_t length = valBuf.used();
    const StringView view(text, length);
    if (view == StringView("true") || view == StringView("false")) {
        return TomlType::Boolean;
    }
    if (length >= 8 && text[2] == ':') {
        return TomlType::LocalTime;
    }
    if (length >= 10 && text[4] == '-') {
        if (length == 10) {
            return TomlType::LocalDate;
        }
        const u8 last = text[length - 1];
        if (last == 'Z' || last == 'z') {
            return TomlType::OffsetDatetime;
        }
        if ((text[length - 6] == '+' || text[length - 6] == '-') && text[length - 3] == ':') {
            return TomlType::OffsetDatetime;
        }
        return TomlType::LocalDatetime;
    }
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
        return TomlType::Integer;
    }
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '.' || text[i] == 'e' || text[i] == 'E' || text[i] == 'n' || text[i] == 'i') {
            return TomlType::Float;
        }
    }
    return TomlType::Integer;
}

bool TomlParser::validDate() const {
    const u8* text = (const u8*)valBuf.data();
    const u32 year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    const u32 month = (text[5] - '0') * 10 + (text[6] - '0');
    const u32 day = (text[8] - '0') * 10 + (text[9] - '0');
    static const u8 lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    u32 limit = lengths[month - 1];
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (month == 2 && leap) {
        limit = 29;
    }
    return day <= limit;
}

bool TomlParser::emitScalarValue() {
    const TomlType type = classifyScalar();
    const bool dated = type == TomlType::OffsetDatetime || type == TomlType::LocalDatetime || type == TomlType::LocalDate;
    if (dated && !validDate()) {
        return fail("no such calendar day");
    }
    const bool keep = sink.tomlScalar(type, StringView(valBuf));
    valBuf.reset();
    if (!keep) {
        aborted = true;
        return false;
    }
    afterValue();
    return true;
}

bool TomlParser::emitStringValue() {
    const bool keep = sink.tomlScalar(TomlType::String, StringView(valBuf));
    valBuf.reset();
    if (!keep) {
        aborted = true;
        return false;
    }
    afterValue();
    return true;
}

bool TomlParser::evArrayBegin() {
    if (!sink.tomlArrayBegin()) {
        aborted = true;
        return false;
    }
    return true;
}

bool TomlParser::evArrayEnd() {
    if (!sink.tomlArrayEnd()) {
        aborted = true;
        return false;
    }
    return true;
}

bool TomlParser::evInlineBegin() {
    if (!sink.tomlInlineTableBegin()) {
        aborted = true;
        return false;
    }
    return true;
}

bool TomlParser::evInlineEnd() {
    if (!sink.tomlInlineTableEnd()) {
        aborted = true;
        return false;
    }
    return true;
}

namespace {
    u32 hexValue(u8 byte) noexcept {
        if (byte >= '0' && byte <= '9') {
            return byte - '0';
        }
        if (byte >= 'a' && byte <= 'f') {
            return byte - 'a' + 10;
        }
        return byte - 'A' + 10;
    }

#define SHITTY_TOML_DATA
#include "toml.rl.h"
#undef SHITTY_TOML_DATA
}

bool parseToml(StringView text, TomlSink& sink) {
    Buffer input;
    const u8 bom[3] = {0xef, 0xbb, 0xbf};
    if (text.length() >= 3 && memcmp(text.data(), bom, 3) == 0) {
        text = StringView(text.data() + 3, text.length() - 3);
    }
    input.append(text.data(), text.length());
    const u8 last = text.empty() ? (u8)'\n' : text[text.length() - 1];
    if (last != '\n' && last != '\r') {
        // The scanner is line-oriented; a synthetic final newline lets the
        // last line finish without special end-of-input handling. It never
        // changes validity: constructs spanning newlines must be closed
        // before one. After a trailing lone CR nothing is appended, or the
        // invalid bare CR would quietly become a CRLF.
        input.append("\n", 1);
    }
    TomlParser parser(sink);
    {
#define SHITTY_TOML_INIT
#include "toml.rl.h"
#undef SHITTY_TOML_INIT
    }
    const u8* p = (const u8*)input.data();
    const u8* pe = p + input.used();
    const u8* eof = pe;
    {
#define SHITTY_TOML_EXEC
#include "toml.rl.h"
#undef SHITTY_TOML_EXEC
    }
    if (parser.aborted) {
        return false;
    }
    const bool unfinished = parser.mode != TomlParser::Mode::LineStart || parser.depth != 0;
    if (parser.failed || parser.cs == toml_error || parser.ts != nullptr || unfinished) {
        sink.tomlError(parser.line, StringView(parser.message));
        return false;
    }
    return true;
}
