/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "grapheme.h"

#include <utf8proc.h>

#include <iterator>

// Bases registered by Unicode emoji-variation-sequences.txt.  Emoji
// variation sequences were introduced in Unicode 9.0 and the registry has
// only grown since (16.0 still adds entries), so this table tracks the
// pinned utf8proc Unicode version rather than being frozen; keeping it
// here makes the streaming width decision independent of font coverage.
static constexpr u32 vs15Bases[] = {
    0x231a,
    0x231b,
    0x23e9,
    0x23ea,
    0x23eb,
    0x23ec,
    0x23f0,
    0x23f3,
    0x25fd,
    0x25fe,
    0x2614,
    0x2615,
    0x2648,
    0x2649,
    0x264a,
    0x264b,
    0x264c,
    0x264d,
    0x264e,
    0x264f,
    0x2650,
    0x2651,
    0x2652,
    0x2653,
    0x267f,
    0x2693,
    0x26a1,
    0x26aa,
    0x26ab,
    0x26bd,
    0x26be,
    0x26c4,
    0x26c5,
    0x26ce,
    0x26d4,
    0x26ea,
    0x26f2,
    0x26f3,
    0x26f5,
    0x26fa,
    0x26fd,
    0x2705,
    0x270a,
    0x270b,
    0x2728,
    0x274c,
    0x274e,
    0x2753,
    0x2754,
    0x2755,
    0x2757,
    0x2795,
    0x2796,
    0x2797,
    0x27b0,
    0x27bf,
    0x2b1b,
    0x2b1c,
    0x2b50,
    0x2b55,
    // Text-default CJK symbols (U+3030, U+303D, U+3297, U+3299) and the
    // Enclosed Ideographic Supplement stay wide under VS15: their text
    // presentation is a full-width form, so narrowing only crops it
    // (matches the unicode-width crate rule; the contour mode-2027 spec
    // keeps VS15 width-neutral entirely).
    0x1f004,
    0x1f30d,
    0x1f30e,
    0x1f30f,
    0x1f315,
    0x1f31c,
    0x1f378,
    0x1f393,
    0x1f3a7,
    0x1f3ac,
    0x1f3ad,
    0x1f3ae,
    0x1f3c2,
    0x1f3c4,
    0x1f3c6,
    0x1f3ca,
    0x1f3e0,
    0x1f3ed,
    0x1f408,
    0x1f415,
    0x1f41f,
    0x1f426,
    0x1f442,
    0x1f446,
    0x1f447,
    0x1f448,
    0x1f449,
    0x1f44d,
    0x1f44e,
    0x1f453,
    0x1f46a,
    0x1f47d,
    0x1f4a3,
    0x1f4b0,
    0x1f4b3,
    0x1f4bb,
    0x1f4bf,
    0x1f4cb,
    0x1f4da,
    0x1f4df,
    0x1f4e4,
    0x1f4e5,
    0x1f4e6,
    0x1f4ea,
    0x1f4eb,
    0x1f4ec,
    0x1f4ed,
    0x1f4f7,
    0x1f4f9,
    0x1f4fa,
    0x1f4fb,
    0x1f508,
    0x1f50d,
    0x1f512,
    0x1f513,
    0x1f550,
    0x1f551,
    0x1f552,
    0x1f553,
    0x1f554,
    0x1f555,
    0x1f556,
    0x1f557,
    0x1f558,
    0x1f559,
    0x1f55a,
    0x1f55b,
    0x1f55c,
    0x1f55d,
    0x1f55e,
    0x1f55f,
    0x1f560,
    0x1f561,
    0x1f562,
    0x1f563,
    0x1f564,
    0x1f565,
    0x1f566,
    0x1f567,
    0x1f610,
    0x1f687,
    0x1f68d,
    0x1f691,
    0x1f694,
    0x1f698,
    0x1f6ad,
    0x1f6b2,
    0x1f6b9,
    0x1f6ba,
    0x1f6bc,
};

static constexpr u32 vs16Bases[] = {
    0x23,
    0x2a,
    0x30,
    0x31,
    0x32,
    0x33,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0xa9,
    0xae,
    0x203c,
    0x2049,
    0x2122,
    0x2139,
    0x2194,
    0x2195,
    0x2196,
    0x2197,
    0x2198,
    0x2199,
    0x21a9,
    0x21aa,
    0x2328,
    0x23cf,
    0x23ed,
    0x23ee,
    0x23ef,
    0x23f1,
    0x23f2,
    0x23f8,
    0x23f9,
    0x23fa,
    0x24c2,
    0x25aa,
    0x25ab,
    0x25b6,
    0x25c0,
    0x25fb,
    0x25fc,
    0x2600,
    0x2601,
    0x2602,
    0x2603,
    0x2604,
    0x260e,
    0x2611,
    0x2618,
    0x261d,
    0x2620,
    0x2622,
    0x2623,
    0x2626,
    0x262a,
    0x262e,
    0x262f,
    0x2638,
    0x2639,
    0x263a,
    0x2640,
    0x2642,
    0x265f,
    0x2660,
    0x2663,
    0x2665,
    0x2666,
    0x2668,
    0x267b,
    0x267e,
    0x2692,
    0x2694,
    0x2695,
    0x2696,
    0x2697,
    0x2699,
    0x269b,
    0x269c,
    0x26a0,
    0x26a7,
    0x26b0,
    0x26b1,
    0x26c8,
    0x26cf,
    0x26d1,
    0x26d3,
    0x26e9,
    0x26f0,
    0x26f1,
    0x26f4,
    0x26f7,
    0x26f8,
    0x26f9,
    0x2702,
    0x2708,
    0x2709,
    0x270c,
    0x270d,
    0x270f,
    0x2712,
    0x2714,
    0x2716,
    0x271d,
    0x2721,
    0x2733,
    0x2734,
    0x2744,
    0x2747,
    0x2763,
    0x2764,
    0x27a1,
    0x2934,
    0x2935,
    0x2b05,
    0x2b06,
    0x2b07,
    0x1f170,
    0x1f171,
    0x1f17e,
    0x1f17f,
    0x1f321,
    0x1f324,
    0x1f325,
    0x1f326,
    0x1f327,
    0x1f328,
    0x1f329,
    0x1f32a,
    0x1f32b,
    0x1f32c,
    0x1f336,
    0x1f37d,
    0x1f396,
    0x1f397,
    0x1f399,
    0x1f39a,
    0x1f39b,
    0x1f39e,
    0x1f39f,
    0x1f3cb,
    0x1f3cc,
    0x1f3cd,
    0x1f3ce,
    0x1f3d4,
    0x1f3d5,
    0x1f3d6,
    0x1f3d7,
    0x1f3d8,
    0x1f3d9,
    0x1f3da,
    0x1f3db,
    0x1f3dc,
    0x1f3dd,
    0x1f3de,
    0x1f3df,
    0x1f3f3,
    0x1f3f5,
    0x1f3f7,
    0x1f43f,
    0x1f441,
    0x1f4fd,
    0x1f549,
    0x1f54a,
    0x1f56f,
    0x1f570,
    0x1f573,
    0x1f574,
    0x1f575,
    0x1f576,
    0x1f577,
    0x1f578,
    0x1f579,
    0x1f587,
    0x1f58a,
    0x1f58b,
    0x1f58c,
    0x1f58d,
    0x1f590,
    0x1f5a5,
    0x1f5a8,
    0x1f5b1,
    0x1f5b2,
    0x1f5bc,
    0x1f5c2,
    0x1f5c3,
    0x1f5c4,
    0x1f5d1,
    0x1f5d2,
    0x1f5d3,
    0x1f5dc,
    0x1f5dd,
    0x1f5de,
    0x1f5e1,
    0x1f5e3,
    0x1f5e8,
    0x1f5ef,
    0x1f5f3,
    0x1f5fa,
    0x1f6cb,
    0x1f6cd,
    0x1f6ce,
    0x1f6cf,
    0x1f6e0,
    0x1f6e1,
    0x1f6e2,
    0x1f6e3,
    0x1f6e4,
    0x1f6e5,
    0x1f6e9,
    0x1f6f0,
    0x1f6f3,
};

static constexpr u32 viramas[] = {
    0x94d,
    0x9cd,
    0xa4d,
    0xacd,
    0xb4d,
    0xbcd,
    0xc4d,
    0xccd,
    0xd4d,
    0xdca,
    0x1039,
    0x17d2,
    0x1a60,
    0x1b44,
    0x1bab,
    0xa806,
    0xa8c4,
    0xa9c0,
    0xaaf6,
    0x10a3f,
    0x11046,
    0x110b9,
    0x11133,
    0x111c0,
    0x11235,
    0x1134d,
    0x113d0,
    0x11442,
    0x114c2,
    0x115bf,
    0x1163f,
    0x116b6,
    0x11839,
    0x1193e,
    0x119e0,
    0x11a47,
    0x11a99,
    0x11c3f,
    0x11d45,
    0x11d97,
    0x11f42,
};

template <size_t Size>
static bool contains(const u32 (&values)[Size], u32 value) {
    size_t low = 0;
    size_t high = Size;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (values[middle] < value) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < Size && values[low] == value;
}

static bool isDefaultWideCjk(u32 codepoint) {
    // UAX #11 section 6.1 assigns Wide to unassigned codepoints in blocks
    // reserved for CJK ideographs.  utf8proc only reports the width of
    // assigned characters, so retain the Unicode default for the holes.
    return (codepoint >= 0x3400 && codepoint <= 0x4dbf) || (codepoint >= 0x4e00 && codepoint <= 0x9fff) || (codepoint >= 0xf900 && codepoint <= 0xfaff) || (codepoint >= 0x20000 && codepoint <= 0x2fffd) || (codepoint >= 0x30000 && codepoint <= 0x3fffd);
}

CodepointProperties codepointProperties(u32 codepoint) {
    const utf8proc_property_t* const property = utf8proc_get_property((i32)(codepoint));
    int width = property->charwidth;
    if (codepoint >= 0x1160 && codepoint <= 0x11ff) {
        // Medial and trailing Hangul Jamo combine with the leading Jamo and
        // do not advance a terminal cursor independently.
        width = 0;
    } else if (width == 1 && (isDefaultWideCjk(codepoint) || (codepoint >= 0x1f1e6 && codepoint <= 0x1f1ff))) {
        width = 2;
    }
    return {
        .width = (u8)(width),
        .simpleGrapheme = property->boundclass == UTF8PROC_BOUNDCLASS_OTHER && property->indic_conjunct_break == UTF8PROC_INDIC_CONJUNCT_BREAK_NONE,
    };
}

int codepointWidth(u32 codepoint) {
    return codepointProperties(codepoint).width;
}

GraphemeWidthEffect graphemeWidthEffect(u32 previous, u32 codepoint) {
    if (codepoint == 0xfe0f && contains(vs16Bases, previous)) {
        return GraphemeWidthEffect::Wide;
    }
    if (codepoint == 0xfe0e && contains(vs15Bases, previous)) {
        return GraphemeWidthEffect::Narrow;
    }

    // Spacing combining marks have positive advance inside a cluster even
    // though their standalone wcwidth is zero.  Viramas and invisible
    // stackers are the exception: they request conjunct formation and the
    // following consonant is what widens the cluster.
    if (!contains(viramas, codepoint) && (codepointWidth(codepoint) > 0 || utf8proc_category((i32)(codepoint)) == UTF8PROC_CATEGORY_MC)) {
        return GraphemeWidthEffect::Wide;
    }
    return GraphemeWidthEffect::Unchanged;
}

bool GraphemeBreaker::breakBeforeSlow(u32 codepoint, bool simple) {
    const bool boundary = utf8proc_grapheme_break_stateful(previous_, (i32)(codepoint), &state_);
    previous_ = (i32)(codepoint);
    previousSimple_ = simple;
    if (boundary) {
        state_ = 0;
    }
    return boundary;
}

bool nextSpanCluster(const u32* codepoints, size_t count, size_t& position, SpanCluster& cluster) {
    if (position >= count) {
        return false;
    }
    GraphemeBreaker breaker;
    cluster.begin = position;
    u32 previous = codepoints[position];
    breaker.breakBefore(previous, codepointProperties(previous).simpleGrapheme);
    int width = codepointWidth(previous);
    ++position;
    while (position < count) {
        const u32 codepoint = codepoints[position];
        if (breaker.breakBefore(codepoint, codepointProperties(codepoint).simpleGrapheme)) {
            break;
        }
        switch (graphemeWidthEffect(previous, codepoint)) {
            case GraphemeWidthEffect::Wide:
                width = 2;
                break;
            case GraphemeWidthEffect::Narrow:
                width = 1;
                break;
            case GraphemeWidthEffect::Unchanged:
                break;
        }
        previous = codepoint;
        ++position;
    }
    cluster.count = position - cluster.begin;
    cluster.cells = (u16)(width < 1 ? 1 : width > 2 ? 2 : width);
    return true;
}
