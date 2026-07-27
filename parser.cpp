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

#include SHITTY_PARSER_GENERATED

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
        if (cs == parser_en_main && *p >= 0x20 && *p < 0x7f && iface.parserAsciiBulkEligible()) {
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
        if (cs == parser_en_main && *p >= 0xc2 && *p <= 0xf4 && iface.parserUtf8BulkEligible()) {
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
