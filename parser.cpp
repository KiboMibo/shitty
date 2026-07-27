/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "parser.h"

#include "color_spec.h"
#include "vterm_trace.h"

#include <std/alg/minmax.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <cstring>

#if defined(__SSE2__)
    #include <emmintrin.h>
#endif

#if defined(SHITTY_COMPACT_PARSER)
    #define SHITTY_PARSER_GENERATED "parser_test.rl.h"
#else
    #define SHITTY_PARSER_GENERATED "parser.rl.h"
#endif

using namespace stl;

namespace {
    [[gnu::always_inline]] size_t printableAsciiPrefix(const u8* input, size_t size) {
        using Bytes = u8 __attribute__((vector_size(16)));
#if !defined(__SSE2__)
        using Bits = unsigned __int128;
#endif
        constexpr Bytes spaces = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        constexpr Bytes deletes = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f};
        size_t offset = 0;
        while (size - offset >= sizeof(Bytes)) {
            Bytes word;
            memcpy(&word, input + offset, sizeof(word));
            const Bytes invalidBytes = (word < spaces) | (word >= deletes);
#if defined(__SSE2__)
            const u32 invalid = _mm_movemask_epi8(__builtin_bit_cast(__m128i, invalidBytes));
            if (invalid != 0) {
                return offset + __builtin_ctz(invalid);
            }
#else
            const Bits invalid = __builtin_bit_cast(Bits, invalidBytes);
            const u64 low = invalid;
            if (low != 0) {
                return offset + __builtin_ctzll(low) / 8;
            }
            const u64 high = invalid >> 64;
            if (high != 0) {
                return offset + 8 + __builtin_ctzll(high) / 8;
            }
#endif
            offset += sizeof(word);
        }
        while (offset < size && input[offset] >= 0x20 && input[offset] < 0x7f) {
            ++offset;
        }
        return offset;
    }

    struct ProtocolParser {
        constexpr const static size_t maxParameters = 32;
        constexpr const static size_t maxDcsBytes = 4095;
        constexpr const static size_t maxOscBytes = 1024 * 1024;

        int state = 0;
        u8 csiPrefix = 0;
        u8 csiIntermediates[4] = {};
        u8 csiIntermediateCount = 0;
        u32 parameters[maxParameters] = {};
        unsigned char separators[maxParameters] = {};
        bool present[maxParameters] = {};
        size_t parameterCount = 0;
        bool csiHadParameters = false;
        Buffer scratch;
        bool overflow = false;
        size_t stringLimit = 0;
        u8 stringUtf8Remaining = 0;
        u8 groundUtf8Remaining = 0;

        u8 dcsIntermediates[4] = {};
        u8 dcsIntermediateCount = 0;
        size_t dcsCapabilityOffset = 0;
        size_t dcsCapabilityDecodedLength = 0;
        u8 dcsCapabilityCandidates = 0;
        u8 dcsCapabilityHighNibble = 0;
        bool dcsCapabilityHasHighNibble = false;
        bool dcsCapabilityValid = false;
        bool dcsCapabilityComplete = false;
        Vector<ParserUdkDefinition> dcsUdkDefinitions;
        Buffer dcsDecoded;
        size_t dcsUdkValueOffset = 0;
        u32 dcsUdkCode = 0;
        VtKey dcsUdkKey = VtKey::NONE;
        u8 dcsUdkHighNibble = 0;
        bool dcsUdkHasCode = false;
        bool dcsUdkHasHighNibble = false;
        bool dcsUdkValid = false;
        bool dcsUdkInValue = false;
        bool dcsUdkHeaderValid = false;
        bool dcsUdkClearDefinitions = false;
        bool dcsUdkLockDefinitions = false;

        u32 oscCommand = 0;
        size_t oscPayloadOffset = 0;
        bool oscCommandValid = false;
        bool oscTerminated = false;
        Buffer oscDecoded;
        u8 oscTitleHighNibble = 0;
        bool oscTitleHex = false;
        bool oscTitleHasHighNibble = false;
        bool oscTitleValid = false;
        bool oscTitleStopped = false;
        u8 oscCwdPercentHigh = 0;
        bool oscCwdValid = false;
        bool oscCwdDecode = false;
        size_t oscHyperlinkIdOffset = 0;
        size_t oscHyperlinkIdLength = 0;
        size_t oscHyperlinkUriOffset = 0;
        bool oscHyperlinkHasId = false;
        u32 oscProgressState = 0;
        u32 oscProgressPercent = 0;
        bool oscProgressStatePresent = false;
        bool oscProgressPercentPresent = false;
        bool oscProgressValid = false;
        Base64Decoder oscBase64;
        u8 osc52ReplySelector = 0;
        bool osc52Primary = false;
        bool osc52Clipboard = false;
        bool osc52SelectorSeen = false;
        bool osc52PayloadSeen = false;
        bool osc52Query = false;
        size_t oscNotificationFieldOffset = 0;
        size_t oscNotificationIdOffset = 0;
        size_t oscNotificationIdLength = 0;
        size_t oscNotificationPayloadOffset = 0;
        u32 oscNotificationPayloadBytes = 0;
        u8 oscNotificationKey = 0;
        bool oscNotificationValid = false;
        bool oscNotificationEncoded = false;
        bool oscNotificationFinal = false;
        bool oscNotificationQuery = false;
        bool oscNotificationClose = false;
        bool oscNotificationBody = false;
        Color oscColor{};
        double oscColorComponents[3]{};
        double oscColorMantissa = 0.0;
        double oscColorFraction = 0.1;
        u64 oscColorHex = 0;
        u32 oscColorExponent = 0;
        u8 oscColorComponent = 0;
        u8 oscColorDigits = 0;
        bool oscColorNegative = false;
        bool oscColorExponentNegative = false;
        bool oscColorValid = false;
        bool oscColorQuery = false;
        bool enterPrinter = false;

        u32 oscFieldNumber = 0;
        u32 oscFieldFirst = 0;
        bool oscFieldNumeric = false;
        bool oscFieldPresent = false;
        bool oscFieldFirstValid = false;
        bool oscFieldHaveFirst = false;
        u8 scsIndex = 0;
        u8 scsMod = 0;
        bool scs96 = false;
    };

    template <bool traced>
    struct ParserImpl final: public Parser {
        ParserImpl(ParserIface& iface, VtermTrace* trace);

        void feed(StringView bytes) override;
        bool consumeStringUtf8Byte(u8 ch);
        bool executeC0(u8 ch);
        bool ragelGroundContinuation(u8 ch);
        void ragelGroundHigh(u8 ch);
        void ragelGroundAscii(u8 ch);
        void ragelAppendStringSpan(const u8* data, size_t size, size_t limit);
        void ragelAppendString(u8 ch, size_t limit);
        void ragelAppendEscapedString(u8 ch, size_t limit);
        void ragelBeginString(VtermTraceString type, bool buffered);
        void ragelBeginDcs();
        void ragelBeginOsc();
        void resetOscColor();
        bool ragelStringContinuation(u8 ch);
        void ragelFinishString();
        void ragelFinishDcs();
        void ragelFinishOsc();
        StringView ragelOscPayload() noexcept;
        void beginCsi();
        u32 parameter(size_t index) const noexcept;
        u32 countParameter(size_t index) const noexcept;
        CsiRectangle rectangle(size_t offset) const noexcept;
        void dispatchScoscSlrm();
        void dispatchStandardModes(bool set);
        void dispatchPrivateModes(bool set);
        void dispatchPrivateSave();
        void dispatchPrivateRestore();
        void dispatchDecfra();
        void dispatchDeccra();
        void dispatchDecera(bool selective);
        void dispatchDeccara(bool reverse);
        void dispatchDecrqcra();
        void dispatchDecll();
        void dispatchDsr(bool privateMode);
        void dispatchTitleMode(bool set);
        void dispatchDecscl();
        void dispatchWindowOps();
        void dispatchDecsle();
        void dispatchXtmodkeys();
        bool parseSgrColor(size_t& index, CellColor& color, int& paletteIndex);
        void dispatchSgr();
        void dispatchMediaCopy(bool privateMode);
        void traceCsi(u8 finalByte);

        ParserIface& iface;
        VtermTrace* parserTrace;
        ProtocolParser parser;
    };

#define SHITTY_PARSER_DATA
#include SHITTY_PARSER_GENERATED
#undef SHITTY_PARSER_DATA
}

template <bool traced>
ParserImpl<traced>::ParserImpl(ParserIface& iface_, VtermTrace* trace)
    : iface(iface_)
    , parserTrace(trace)
{
    int& cs = parser.state;
#define SHITTY_PARSER_INIT
#include SHITTY_PARSER_GENERATED
#undef SHITTY_PARSER_INIT
}

template <bool traced>
bool ParserImpl<traced>::consumeStringUtf8Byte(u8 ch) {
    if (parser.stringUtf8Remaining != 0) {
        if ((ch & 0xc0) == 0x80) {
            --parser.stringUtf8Remaining;
            return true;
        }
        parser.stringUtf8Remaining = 0;
    }

    if (ch >= 0xc2 && ch <= 0xdf) {
        parser.stringUtf8Remaining = 1;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        parser.stringUtf8Remaining = 2;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        parser.stringUtf8Remaining = 3;
    }
    return false;
}

template <bool traced>
bool ParserImpl<traced>::executeC0(u8 ch) {
    if (ch >= 0x20 || ch == '\x18' || ch == '\x1a' || ch == '\x1b') {
        return false;
    }
    if (ch == '\a') {
        iface.parserBell();
        return true;
    }
    if (ch == '\x0e') {
        iface.parserLockingShiftGl(1);
        return true;
    }
    if (ch == '\x0f') {
        iface.parserLockingShiftGl(0);
        return true;
    }
    switch (ch) {
        case '\b':
            iface.parserMoveCursorBackward(1);
            break;
        case '\t':
            iface.inp_HT();
            break;
        case '\n':
        case '\v':
        case '\f':
            iface.esc_IND();
            break;
        case '\r':
            iface.inp_CR();
            break;
        default:
            break;
    }
    return true;
}

template <bool traced>
bool ParserImpl<traced>::ragelGroundContinuation(u8 ch) {
    if (parser.groundUtf8Remaining == 0 || ch < 0x80) {
        return false;
    }
    if constexpr (traced) {
        parserTrace->text(&ch, 1);
    }
    iface.parserGroundHigh(ch);
    if ((ch & 0xc0) == 0x80) {
        --parser.groundUtf8Remaining;
    } else if (ch >= 0xc2 && ch <= 0xdf) {
        parser.groundUtf8Remaining = 1;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        parser.groundUtf8Remaining = 2;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        parser.groundUtf8Remaining = 3;
    } else {
        parser.groundUtf8Remaining = 0;
    }
    return true;
}

template <bool traced>
void ParserImpl<traced>::ragelGroundHigh(u8 ch) {
    if (ragelGroundContinuation(ch)) {
        return;
    }
    if constexpr (traced) {
        if (ch >= 0xa0) {
            parserTrace->text(&ch, 1);
        } else {
            parserTrace->control(ch);
        }
    }
    if (ch <= 0x9f) {
        iface.parserResetGraphemeInput();
    }
    iface.parserGroundHigh(ch);
    if (!iface.parserGroundUtf8Enabled()) {
        parser.groundUtf8Remaining = 0;
    } else if (ch >= 0xc2 && ch <= 0xdf) {
        parser.groundUtf8Remaining = 1;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        parser.groundUtf8Remaining = 2;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        parser.groundUtf8Remaining = 3;
    } else {
        parser.groundUtf8Remaining = 0;
    }
}

template <bool traced>
void ParserImpl<traced>::ragelGroundAscii(u8 ch) {
    parser.groundUtf8Remaining = 0;
    if constexpr (traced) {
        parserTrace->text(&ch, 1);
    }
    iface.parserGroundAscii(ch);
}

template <bool traced>
void ParserImpl<traced>::ragelAppendStringSpan(const u8* data, size_t size, size_t limit) {
    if constexpr (traced) {
        parserTrace->stringData(data, size);
    }
    const size_t used = parser.scratch.used();
    const size_t available = used < limit ? limit - used : 0;
    const size_t appendSize = min(size, available);
    if (appendSize != 0) {
        parser.scratch.append(data, appendSize);
    }
    if (appendSize != size) {
        parser.overflow = true;
    }
}

template <bool traced>
void ParserImpl<traced>::ragelAppendString(u8 ch, size_t limit) {
    ragelAppendStringSpan(&ch, 1, limit);
}

template <bool traced>
void ParserImpl<traced>::ragelAppendEscapedString(u8 ch, size_t limit) {
    const u8 bytes[] = {'\x1b', ch};
    ragelAppendStringSpan(bytes, sizeof(bytes), limit);
}

template <bool traced>
void ParserImpl<traced>::ragelBeginString(VtermTraceString type, bool buffered) {
    iface.parserResetGraphemeInput();
    parser.stringUtf8Remaining = 0;
    parser.stringLimit = type == VtermTraceString::Dcs ? parser.maxDcsBytes : type == VtermTraceString::Osc ? parser.maxOscBytes : 0;
    parser.oscCwdDecode = false;
    if (buffered) {
        parser.scratch.reset();
        parser.overflow = false;
    }
    if constexpr (traced) {
        parserTrace->stringBegin(type);
    }
}

template <bool traced>
void ParserImpl<traced>::ragelBeginDcs() {
    ragelBeginString(VtermTraceString::Dcs, true);
    parser.parameters[0] = 0;
    parser.separators[0] = 0;
    parser.present[0] = false;
    parser.parameterCount = 1;
    parser.dcsIntermediateCount = 0;
    parser.dcsCapabilityOffset = 0;
    parser.dcsCapabilityDecodedLength = 0;
    parser.dcsCapabilityCandidates = 0;
    parser.dcsCapabilityHasHighNibble = false;
    parser.dcsCapabilityValid = false;
    parser.dcsCapabilityComplete = false;
    parser.dcsUdkDefinitions.clear();
    parser.dcsDecoded.reset();
    parser.dcsUdkValueOffset = 0;
    parser.dcsUdkCode = 0;
    parser.dcsUdkKey = VtKey::NONE;
    parser.dcsUdkHasCode = false;
    parser.dcsUdkHasHighNibble = false;
    parser.dcsUdkValid = false;
    parser.dcsUdkInValue = false;
    parser.dcsUdkHeaderValid = false;
    parser.dcsUdkClearDefinitions = false;
    parser.dcsUdkLockDefinitions = false;
}

template <bool traced>
void ParserImpl<traced>::ragelBeginOsc() {
    ragelBeginString(VtermTraceString::Osc, true);
    parser.oscCommand = 0;
    parser.oscPayloadOffset = 0;
    parser.oscCommandValid = false;
    parser.oscTerminated = false;
    parser.oscDecoded.reset();
    parser.oscTitleHighNibble = 0;
    parser.oscTitleHex = false;
    parser.oscTitleHasHighNibble = false;
    parser.oscTitleValid = false;
    parser.oscTitleStopped = false;
    parser.oscCwdPercentHigh = 0;
    parser.oscCwdValid = false;
    parser.oscCwdDecode = false;
    parser.oscHyperlinkIdOffset = 0;
    parser.oscHyperlinkIdLength = 0;
    parser.oscHyperlinkUriOffset = 0;
    parser.oscHyperlinkHasId = false;
    parser.oscProgressState = 0;
    parser.oscProgressPercent = 0;
    parser.oscProgressStatePresent = false;
    parser.oscProgressPercentPresent = false;
    parser.oscProgressValid = false;
    parser.oscBase64.reset();
    parser.osc52ReplySelector = 0;
    parser.osc52Primary = false;
    parser.osc52Clipboard = false;
    parser.osc52SelectorSeen = false;
    parser.osc52PayloadSeen = false;
    parser.osc52Query = false;
}

template <bool traced>
void ParserImpl<traced>::resetOscColor() {
    parser.oscColor = {};
    parser.oscColorComponents[0] = 0.0;
    parser.oscColorComponents[1] = 0.0;
    parser.oscColorComponents[2] = 0.0;
    parser.oscColorHex = 0;
    parser.oscColorComponent = 0;
    parser.oscColorDigits = 0;
    parser.oscColorValid = true;
    parser.oscColorQuery = false;
}

template <bool traced>
bool ParserImpl<traced>::ragelStringContinuation(u8 ch) {
    if (!consumeStringUtf8Byte(ch)) {
        return false;
    }
    if (parser.stringLimit != 0) {
        ragelAppendString(ch, parser.stringLimit);
    } else if constexpr (traced) {
        parserTrace->stringData(&ch, 1);
    }
    if (parser.oscCwdDecode && !parser.overflow) {
        parser.oscDecoded.append(&ch, 1);
    }
    return true;
}

template <bool traced>
void ParserImpl<traced>::ragelFinishString() {
    parser.stringUtf8Remaining = 0;
    parser.stringLimit = 0;
    if constexpr (traced) {
        parserTrace->stringEnd();
    }
}

template <bool traced>
void ParserImpl<traced>::ragelFinishDcs() {
    ragelFinishString();
}

template <bool traced>
void ParserImpl<traced>::ragelFinishOsc() {
    ragelFinishString();
}

template <bool traced>
StringView ParserImpl<traced>::ragelOscPayload() noexcept {
    const auto* data = (const u8*)(parser.scratch.data());
    return StringView(data + parser.oscPayloadOffset, parser.scratch.used() - parser.oscPayloadOffset);
}

template <bool traced>
void ParserImpl<traced>::beginCsi() {
    parser.stringUtf8Remaining = 0;
    parser.stringLimit = 0;
    iface.parserResetGraphemeInput();
    parser.parameters[0] = 0;
    parser.separators[0] = 0;
    parser.present[0] = false;
    parser.parameterCount = 1;
    parser.csiHadParameters = false;
    parser.enterPrinter = false;
    parser.csiPrefix = 0;
    parser.csiIntermediateCount = 0;
}

template <bool traced>
u32 ParserImpl<traced>::parameter(size_t index) const noexcept {
    return index < parser.parameterCount ? parser.parameters[index] : 0;
}

template <bool traced>
u32 ParserImpl<traced>::countParameter(size_t index) const noexcept {
    const u32 value = parameter(index);
    return value ? value : 1;
}

template <bool traced>
CsiRectangle ParserImpl<traced>::rectangle(size_t offset) const noexcept {
    return {
        parameter(offset),
        parameter(offset + 1),
        parameter(offset + 2),
        parameter(offset + 3),
    };
}

template <bool traced>
void ParserImpl<traced>::dispatchScoscSlrm() {
    if (iface.horizontalMarginMode()) {
        iface.csi_SLRM(parameter(0), parameter(1), parser.parameterCount <= 2);
    } else {
        iface.esc_DECSC();
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchStandardModes(bool set) {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        if (set) {
            iface.csi_SM(parser.parameters[index]);
        } else {
            iface.csi_RM(parser.parameters[index]);
        }
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchPrivateModes(bool set) {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        if (set) {
            iface.csi_privSM(parser.parameters[index]);
        } else {
            iface.csi_privRM(parser.parameters[index]);
        }
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchPrivateSave() {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        iface.csi_privSave(parser.parameters[index]);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchPrivateRestore() {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        iface.csi_privRestore(parser.parameters[index]);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDecfra() {
    iface.csi_DECFRA(parameter(0), rectangle(1));
}

template <bool traced>
void ParserImpl<traced>::dispatchDeccra() {
    iface.csi_DECCRA(rectangle(0), countParameter(5), countParameter(6));
}

template <bool traced>
void ParserImpl<traced>::dispatchDecera(bool selective) {
    iface.csi_DECERA(rectangle(0), selective);
}

template <bool traced>
void ParserImpl<traced>::dispatchDeccara(bool reverse) {
    if (parser.parameterCount < 5) {
        return;
    }
    const CsiRectangle area = rectangle(0);
    for (size_t index = 4; index < parser.parameterCount; ++index) {
        iface.csi_DECCARA(area, parser.parameters[index], reverse);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDecrqcra() {
    if (parser.parameterCount >= 6) {
        iface.csi_DECRQCRA(parameter(0), rectangle(2));
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDecll() {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        iface.csi_DECLL(parser.parameters[index], index + 1 == parser.parameterCount);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDsr(bool privateMode) {
    const u32 operation = parameter(0);
    if (!privateMode) {
        if (operation == 5) {
            iface.dsrOperatingStatus();
        } else if (operation == 6) {
            iface.dsrCursorPosition(false);
        }
        return;
    }
    switch (operation) {
        case 6:
            iface.dsrCursorPosition(true);
            break;
        case 15:
            iface.dsrPrinterStatus();
            break;
        case 25:
            iface.dsrUserDefinedKeys();
            break;
        case 26:
            iface.dsrKeyboard();
            break;
        case 55:
            iface.dsrLocator();
            break;
        case 56:
            iface.dsrLocatorType();
            break;
        case 62:
            iface.dsrMacroSpace();
            break;
        case 63:
            iface.dsrMemoryChecksum(parameter(1));
            break;
        case 75:
            iface.dsrDataIntegrity();
            break;
        case 85:
            iface.dsrMultipleSession();
            break;
        case 996:
            iface.dsrColorScheme();
            break;
        default:
            break;
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchTitleMode(bool set) {
    if (!parser.csiHadParameters) {
        iface.csi_XTTITLEMODE(0, set, true);
        return;
    }
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        iface.csi_XTTITLEMODE(parser.parameters[index], set, false);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDecscl() {
    CompatibilityLevel level;
    switch (parameter(0)) {
        case 61:
            level = CompatibilityLevel::VT100;
            break;
        case 62:
            level = CompatibilityLevel::VT200;
            break;
        case 63:
            level = CompatibilityLevel::VT300;
            break;
        case 64:
            level = CompatibilityLevel::VT400;
            break;
        case 65:
            level = CompatibilityLevel::VT500;
            break;
        default:
            return;
    }
    const u32 controlMode = parameter(1);
    if (controlMode <= 2) {
        iface.csi_DECSCL(level, level != CompatibilityLevel::VT100 && controlMode != 1);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchWindowOps() {
    if (!iface.windowOperationsAllowed()) {
        return;
    }
    const u32 operation = parameter(0);
    const u32 first = parameter(1);
    const u32 second = parameter(2);
    const bool firstPresent = parser.parameterCount > 1 && parser.present[1];
    const bool secondPresent = parser.parameterCount > 2 && parser.present[2];
    switch (operation) {
        case 4:
            iface.xtResizePixels(first, firstPresent, second, secondPresent);
            break;
        case 8:
            iface.xtResizeCells(first, firstPresent, second, secondPresent);
            break;
        case 1:
        case 2:
        case 3:
        case 5:
        case 6:
        case 7:
        case 9:
        case 10:
            iface.xtWindowOperation(operation, first, second);
            break;
        case 11:
            iface.xtReportWindowState();
            break;
        case 13:
            iface.xtReportWindowPosition();
            break;
        case 14:
            iface.xtReportWindowPixelSize(firstPresent && first == 2);
            break;
        case 15:
            iface.xtReportScreenPixelSize();
            break;
        case 16:
            iface.xtReportCellSize();
            break;
        case 18:
            iface.xtReportGridSize();
            break;
        case 19:
            iface.xtReportScreenGridSize();
            break;
        case 20:
            iface.xtReportIconTitle();
            break;
        case 21:
            iface.xtReportWindowTitle();
            break;
        case 22:
            iface.xtPushTitle(first);
            break;
        case 23:
            iface.xtPopTitle(first);
            break;
        default:
            if (operation >= 24) {
                iface.xtResizeRows(operation);
            }
            break;
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchDecsle() {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        iface.csi_DECSLE(parser.parameters[index]);
    }
}

template <bool traced>
void ParserImpl<traced>::dispatchXtmodkeys() {
    iface.csi_XTMODKEYS(parameter(0), parameter(1), parser.parameterCount > 1, !parser.csiHadParameters);
}

template <bool traced>
bool ParserImpl<traced>::parseSgrColor(size_t& index, CellColor& color, int& paletteIndex) {
    if (index + 1 >= parser.parameterCount) {
        return false;
    }
    const bool colon = parser.separators[index + 1] == ':';
    const u32 mode = parser.parameters[++index];
    if (colon) {
        const size_t first = index + 1;
        size_t end = index;
        while (end + 1 < parser.parameterCount && parser.separators[end + 1] == ':') {
            ++end;
        }
        index = end;

        if (mode == 5) {
            if (end - first + 1 != 1 || parser.parameters[first] > 255) {
                return false;
            }
            paletteIndex = parser.parameters[first];
            color = CellColor::indexed(paletteIndex);
            return true;
        }
        const size_t count = end - first + 1;
        const size_t rgbFirst = first + (count >= 4);
        if (mode != 2 || count < 3 || (count == 3 && !parser.present[first]) || !parser.present[rgbFirst] || !parser.present[rgbFirst + 1] || !parser.present[rgbFirst + 2] || parser.parameters[rgbFirst] > 255 || parser.parameters[rgbFirst + 1] > 255 || parser.parameters[rgbFirst + 2] > 255) {
            return false;
        }
        paletteIndex = -1;
        color = CellColor::direct({
            (u8)(parser.parameters[rgbFirst]),
            (u8)(parser.parameters[rgbFirst + 1]),
            (u8)(parser.parameters[rgbFirst + 2]),
        });
        return true;
    }

    if (mode == 5) {
        if (index + 1 >= parser.parameterCount) {
            return false;
        }
        const u32 value = parser.parameters[++index];
        if (value > 255) {
            return false;
        }
        paletteIndex = value;
        color = CellColor::indexed(value);
        return true;
    }
    if (mode != 2) {
        return false;
    }

    const size_t first = index + 1;
    const size_t available = parser.parameterCount - first;
    index += min<size_t>(available, 3);
    if (available < 3 || parser.parameters[first] > 255 || parser.parameters[first + 1] > 255 || parser.parameters[first + 2] > 255) {
        return false;
    }
    paletteIndex = -1;
    color = CellColor::direct({
        (u8)(parser.parameters[first]),
        (u8)(parser.parameters[first + 1]),
        (u8)(parser.parameters[first + 2]),
    });
    index = first + 2;
    return true;
}

template <bool traced>
void ParserImpl<traced>::dispatchSgr() {
    for (size_t index = 0; index < parser.parameterCount; ++index) {
        const u32 attribute = parser.parameters[index];
        switch (attribute) {
            case 0:
                iface.sgrReset();
                break;
            case 1:
                iface.sgrBold(true);
                break;
            case 2:
                iface.sgrFaint(true);
                break;
            case 3:
                iface.sgrItalic(true);
                break;
            case 4:
                if (index + 1 < parser.parameterCount && parser.separators[index + 1] == ':') {
                    const u32 style = parser.parameters[++index];
                    if (style <= 5) {
                        iface.sgrUnderline(style);
                    }
                } else {
                    iface.sgrUnderline(1);
                }
                break;
            case 5:
            case 6:
                iface.sgrBlink(true);
                break;
            case 7:
                iface.sgrInverse(true);
                break;
            case 8:
                iface.sgrConceal(true);
                break;
            case 9:
                iface.sgrStrike(true);
                break;
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
                break;
            case 21:
                iface.sgrUnderline(2);
                break;
            case 22:
                iface.sgrBold(false);
                iface.sgrFaint(false);
                break;
            case 23:
                iface.sgrItalic(false);
                break;
            case 24:
                iface.sgrUnderline(0);
                break;
            case 25:
                iface.sgrBlink(false);
                break;
            case 27:
                iface.sgrInverse(false);
                break;
            case 28:
                iface.sgrConceal(false);
                break;
            case 29:
                iface.sgrStrike(false);
                break;
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
                iface.sgrForeground(CellColor::indexed(attribute - 30), attribute - 30, true);
                break;
            case 38: {
                CellColor color{};
                int paletteIndex;
                if (parseSgrColor(index, color, paletteIndex)) {
                    iface.sgrForeground(color, paletteIndex, false);
                }
            } break;
            case 39:
                iface.sgrDefaultForeground();
                break;
            case 40:
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
                iface.sgrBackground(CellColor::indexed(attribute - 40), attribute - 40);
                break;
            case 48: {
                CellColor color{};
                int paletteIndex;
                if (parseSgrColor(index, color, paletteIndex)) {
                    iface.sgrBackground(color, paletteIndex);
                }
            } break;
            case 49:
                iface.sgrDefaultBackground();
                break;
            case 53:
                iface.sgrOverline(true);
                break;
            case 55:
                iface.sgrOverline(false);
                break;
            case 58: {
                CellColor color{};
                int paletteIndex;
                if (parseSgrColor(index, color, paletteIndex)) {
                    iface.sgrUnderlineColor(color, paletteIndex);
                }
            } break;
            case 59:
                iface.sgrDefaultUnderlineColor();
                break;
            case 90:
            case 91:
            case 92:
            case 93:
            case 94:
            case 95:
            case 96:
            case 97:
                iface.sgrForeground(CellColor::indexed(attribute - 82), attribute - 82, false);
                break;
            case 100:
            case 101:
            case 102:
            case 103:
            case 104:
            case 105:
            case 106:
            case 107:
                iface.sgrBackground(CellColor::indexed(attribute - 92), attribute - 92);
                break;
            default:
                break;
        }
    }
    iface.sgrFinish();
}

template <bool traced>
void ParserImpl<traced>::dispatchMediaCopy(bool privateMode) {
    const u32 operation = parser.parameters[0];
    if (privateMode) {
        if (operation == 1) {
            iface.mediaCopyLine();
        } else if (operation == 4) {
            iface.setAutoPrint(false);
        } else if (operation == 5) {
            iface.setAutoPrint(true);
        }
    } else if (operation == 0) {
        iface.mediaCopyScreen();
    }
    parser.enterPrinter = !privateMode && operation == 5;
}

template <bool traced>
void ParserImpl<traced>::traceCsi(u8 finalByte) {
    if constexpr (traced) {
        parserTrace->csi(finalByte, StringView(&parser.csiPrefix, parser.csiPrefix == 0 ? 0 : 1), StringView(parser.csiIntermediates, parser.csiIntermediateCount), parser.parameters, parser.separators, parser.parameterCount, parser.csiHadParameters);
    }
}

template <bool traced>
void ParserImpl<traced>::feed(StringView bytes) {
    const u8* p = bytes.data();
    const u8* const pe = p + bytes.length();
    const u8* const eof = nullptr;
    int& cs = parser.state;
    const auto appendPrinter = [&](const void* data, size_t size) {
        if (size != 0 && iface.parserHandlesPrinter()) {
            iface.parserPrint(StringView((const u8*)(data), size));
        }
    };

    while (p != pe) {
        if (cs == parser_en_printer) {
            const size_t remaining = pe - p;
            const u8* escape = (const u8*)memchr(p, 0x1b, remaining);
            const size_t beforeEscape = escape == nullptr ? remaining : escape - p;
            const u8* csi = (const u8*)memchr(p, 0x9b, beforeEscape);
            const u8* next = csi == nullptr ? escape : csi;
            const size_t count = next == nullptr ? remaining : next - p;
            appendPrinter(p, count);
            p += count;
            if (p == pe) {
                continue;
            }
        }
        if (cs == parser_en_main && parser.groundUtf8Remaining == 0 && *p >= 0x20 && *p < 0x7f && iface.parserAsciiBulkEligible()) {
            const size_t lines = iface.parserPlaceAsciiLines(StringView(p, pe - p));
            if (lines != 0) {
                if constexpr (traced) {
                    const u8* trace = p;
                    const u8* const traceEnd = p + lines;
                    while (trace != traceEnd) {
                        const u8* carriageReturn = (const u8*)memchr(trace, '\r', traceEnd - trace);
                        parserTrace->text(trace, carriageReturn - trace);
                        parserTrace->control('\r');
                        parserTrace->control('\n');
                        trace = carriageReturn + 2;
                    }
                }
                p += lines;
                continue;
            }
            const size_t count = printableAsciiPrefix(p, pe - p);
            if constexpr (traced) {
                parserTrace->text(p, count);
            }
            iface.parserPlaceAsciiRun(StringView(p, count));
            p += count;
            if (p + 1 < pe && p[0] == '\r' && p[1] == '\n') {
                if constexpr (traced) {
                    parserTrace->control('\r');
                    parserTrace->control('\n');
                }
                iface.parserResetGraphemeInput();
                iface.inp_CR();
                if (iface.parserAutoNewlineMode()) {
                    iface.inp_CR();
                }
                iface.esc_IND();
                p += 2;
            }
            continue;
        }
        if (cs == parser_en_main && parser.groundUtf8Remaining == 0 && *p >= 0xc2 && *p <= 0xf4 && iface.parserUtf8BulkEligible()) {
            const size_t consumed = iface.parserPlaceUtf8Run(StringView(p, pe - p));
            if (consumed > 0) {
                if constexpr (traced) {
                    parserTrace->text(p, consumed);
                }
                p += consumed;
                continue;
            }
        }

#define SHITTY_PARSER_EXEC
#include SHITTY_PARSER_GENERATED
#undef SHITTY_PARSER_EXEC
    }
}

Parser* Parser::create(ObjPool* pool, ParserIface& iface, VtermTrace* trace) {
    if (trace != nullptr) {
        return pool->make<ParserImpl<true>>(iface, trace);
    }
    return pool->make<ParserImpl<false>>(iface, trace);
}

#undef SHITTY_PARSER_GENERATED
