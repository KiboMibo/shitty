/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "grapheme.h"

#include "unicode.h"

namespace {
    static bool isDefaultWideCjk(u32 codepoint) {
        // UAX #11 section 6.1 assigns Wide to unassigned codepoints in blocks
        // reserved for CJK ideographs. The base property table deliberately
        // keeps the historical width-one fallback for unassigned characters,
        // so retain the Unicode default for these holes here.
        return (codepoint >= 0x3400 && codepoint <= 0x4dbf) || (codepoint >= 0x4e00 && codepoint <= 0x9fff) || (codepoint >= 0xf900 && codepoint <= 0xfaff) || (codepoint >= 0x20000 && codepoint <= 0x2fffd) || (codepoint >= 0x30000 && codepoint <= 0x3fffd);
    }

    static bool graphemeBreakSimple(GraphemeClass left, GraphemeClass right) {
        if (left == GraphemeClass::Cr && right == GraphemeClass::Lf) {
            return false;
        }
        if (left == GraphemeClass::Cr || left == GraphemeClass::Lf || left == GraphemeClass::Control) {
            return true;
        }
        if (right == GraphemeClass::Cr || right == GraphemeClass::Lf || right == GraphemeClass::Control) {
            return true;
        }
        if (left == GraphemeClass::HangulL && (right == GraphemeClass::HangulL || right == GraphemeClass::HangulV || right == GraphemeClass::HangulLv || right == GraphemeClass::HangulLvt)) {
            return false;
        }
        if ((left == GraphemeClass::HangulLv || left == GraphemeClass::HangulV) && (right == GraphemeClass::HangulV || right == GraphemeClass::HangulT)) {
            return false;
        }
        if ((left == GraphemeClass::HangulLvt || left == GraphemeClass::HangulT) && right == GraphemeClass::HangulT) {
            return false;
        }
        if (right == GraphemeClass::Extend || right == GraphemeClass::Zwj || right == GraphemeClass::SpacingMark || left == GraphemeClass::Prepend) {
            return false;
        }
        if (left == GraphemeClass::EmojiZwj && right == GraphemeClass::ExtendedPictographic) {
            return false;
        }
        if (left == GraphemeClass::RegionalIndicator && right == GraphemeClass::RegionalIndicator) {
            return false;
        }
        return true;
    }
}

UnicodeWidths::UnicodeWidths(u32 level)
    : level_(level)
{
}

u32 UnicodeWidths::level() const {
    return level_ != 0 ? level_ : unicodeVersion();
}

CodepointProperties UnicodeWidths::codepointProperties(u32 codepoint) const {
    const UnicodeCodepointProperties property = unicodeCodepointProperties(codepoint);
    int width = property.width;
    if (codepoint >= 0x1160 && codepoint <= 0x11ff) {
        // Medial and trailing Hangul Jamo combine with the leading Jamo and
        // do not advance a terminal cursor independently.
        width = 0;
    } else if (width == 1 && (isDefaultWideCjk(codepoint) || (codepoint >= 0x1f1e6 && codepoint <= 0x1f1ff))) {
        width = 2;
    }
    if (width == 2 && level_ != 0 && level_ < 16 && codepoint < 0x20000) {
        // A lowered level undoes the East Asian Width reclassifications
        // younger than it: the 15.1 trigram batch, and below 9 the emoji
        // batch too. Only this cold path pays; every caller sits behind
        // a per-terminal property cache.
        if (unicodeWideSince16(codepoint) || (level_ < 9 && unicodeWideSince9(codepoint))) {
            width = 1;
        }
    }
    return {
        .width = (u8)(width),
        .simpleGrapheme = property.graphemeClass == GraphemeClass::Other && property.indicConjunctClass == IndicConjunctClass::None,
    };
}

int UnicodeWidths::codepointWidth(u32 codepoint) const {
    return codepointProperties(codepoint).width;
}

bool emojiPresentation(u32 codepoint) {
    // The supplementary emoji planes render as emoji by default; the BMP
    // set is exactly the bases the variation-sequence registry lets VS15
    // downgrade to text.
    if (codepoint >= 0x1f000 && codepoint <= 0x1faff) {
        return true;
    }
    return unicodeCodepointProperties(codepoint).narrowsWithVs15;
}

GraphemeWidthEffect UnicodeWidths::graphemeWidthEffect(u32 previous, u32 codepoint) const {
    const UnicodeCodepointProperties previousProperties = unicodeCodepointProperties(previous);
    if (codepoint == 0xfe0f && previousProperties.widensWithVs16) {
        return GraphemeWidthEffect::Wide;
    }
    if (codepoint == 0xfe0e && previousProperties.narrowsWithVs15) {
        return GraphemeWidthEffect::Narrow;
    }

    // Spacing combining marks have positive advance inside a cluster even
    // though their standalone wcwidth is zero.  Viramas and invisible
    // stackers are the exception: they request conjunct formation and the
    // following consonant is what widens the cluster.
    const UnicodeCodepointProperties properties = unicodeCodepointProperties(codepoint);
    if (!properties.virama && (codepointWidth(codepoint) > 0 || properties.category == GeneralCategory::SpacingMark)) {
        return GraphemeWidthEffect::Wide;
    }
    return GraphemeWidthEffect::Unchanged;
}

bool GraphemeBreaker::breakBeforeSlow(u32 codepoint, bool simple) {
    const UnicodeCodepointProperties left = unicodeCodepointProperties((u32)(previous_));
    const UnicodeCodepointProperties right = unicodeCodepointProperties(codepoint);
    GraphemeClass stateClass;
    IndicConjunctClass stateIndic;
    if (state_ == 0) {
        stateClass = left.graphemeClass;
        stateIndic = left.indicConjunctClass == IndicConjunctClass::Consonant ? IndicConjunctClass::Consonant : IndicConjunctClass::None;
    } else {
        stateClass = (GraphemeClass)((state_ & 0xff) - 1);
        stateIndic = (IndicConjunctClass)(state_ >> 8);
    }

    const bool boundary = graphemeBreakSimple(stateClass, right.graphemeClass) && !(stateIndic == IndicConjunctClass::Linker && right.indicConjunctClass == IndicConjunctClass::Consonant);
    if (right.indicConjunctClass == IndicConjunctClass::Consonant || stateIndic == IndicConjunctClass::Consonant || stateIndic == IndicConjunctClass::Extend) {
        stateIndic = right.indicConjunctClass;
    } else if (stateIndic == IndicConjunctClass::Linker) {
        stateIndic = right.indicConjunctClass == IndicConjunctClass::Extend ? IndicConjunctClass::Linker : right.indicConjunctClass;
    }

    if (stateClass == right.graphemeClass && right.graphemeClass == GraphemeClass::RegionalIndicator) {
        stateClass = GraphemeClass::Other;
    } else if (stateClass == GraphemeClass::ExtendedPictographic) {
        if (right.graphemeClass == GraphemeClass::Extend) {
            stateClass = GraphemeClass::ExtendedPictographic;
        } else if (right.graphemeClass == GraphemeClass::Zwj) {
            stateClass = GraphemeClass::EmojiZwj;
        } else {
            stateClass = right.graphemeClass;
        }
    } else {
        stateClass = right.graphemeClass;
    }
    state_ = ((i32)(stateClass) + 1) | ((i32)(stateIndic) << 8);
    previous_ = (i32)(codepoint);
    previousSimple_ = simple;
    if (boundary) {
        state_ = 0;
    }
    return boundary;
}

bool UnicodeWidths::nextSpanCluster(const u32* codepoints, size_t count, size_t& position, SpanCluster& cluster) const {
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
