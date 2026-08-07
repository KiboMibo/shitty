/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include <stdint.h>

struct CodepointProperties {
    u8 width;
    bool simpleGrapheme;
};

CodepointProperties codepointProperties(u32 codepoint);
int codepointWidth(u32 codepoint);
// Which Unicode version's widths the cells emulate: East Asian Width
// reclassifications younger than the level are undone, so the grid
// agrees with the libc the shells at the pty's far end measure with.
// 0 means the full current tables. Set once at startup, before any
// terminal exists - the per-terminal property caches never see two
// answers for one codepoint.
void setUnicodeWidthLevel(u32 level);
// The effective version, for feature reporting: the configured level,
// or the utf8proc Unicode major when running the full tables.
u32 unicodeWidthLevel();
// Whether the codepoint's default presentation is emoji.
bool emojiPresentation(u32 codepoint);

enum class GraphemeWidthEffect {
    Unchanged,
    Wide,
    Narrow,
};

GraphemeWidthEffect graphemeWidthEffect(u32 previous, u32 codepoint);

// The codepoints of one stored grapheme cluster, viewed in place.
struct GraphemeView {
    const u32* values = nullptr;
    u32 count = 0;

    const u32* begin() const noexcept {
        return values;
    }

    const u32* end() const noexcept {
        return count == 0 ? values : values + count;
    }

    const u32* data() const noexcept {
        return values;
    }

    size_t size() const noexcept {
        return count;
    }

    bool empty() const noexcept {
        return count == 0;
    }

    const u32& operator[](size_t index) const noexcept {
        return values[index];
    }
};

// One cluster of a span's flat codepoint string: codepoints
// [begin, begin + count) covering cells grid cells.
struct SpanCluster {
    size_t begin = 0;
    size_t count = 0;
    u16 cells = 0;
};

// Iterates the grapheme clusters of a span string with their grid widths,
// by the same width rules the terminal used to place the cells. position
// advances past the cluster; returns false at the end of the string.
bool nextSpanCluster(const u32* codepoints, size_t count, size_t& position, SpanCluster& cluster);

class GraphemeBreaker {
public:
    [[gnu::always_inline]] bool breakBefore(u32 codepoint) {
        return breakBefore(codepoint, codepoint >= 0x20 && codepoint < 0x7f);
    }

    [[gnu::always_inline]] bool breakBefore(u32 codepoint, bool simple) {
        if (!hasPrevious_) {
            hasPrevious_ = true;
            previous_ = (i32)(codepoint);
            previousSimple_ = simple;
            return true;
        }

        if (previousSimple_ && simple) {
            previous_ = (i32)(codepoint);
            previousSimple_ = true;
            state_ = 0;
            return true;
        }

        return breakBeforeSlow(codepoint, simple);
    }

    [[gnu::always_inline]] void setBoundaryAfter(u32 codepoint) {
        setBoundaryAfter(codepoint, codepoint >= 0x20 && codepoint < 0x7f);
    }

    // For callers that already know the codepoint's grapheme simplicity and
    // batch their boundary checks: equivalent to a breakBefore(codepoint,
    // simple) that returned true.
    [[gnu::always_inline]] void setBoundaryAfter(u32 codepoint, bool simple) {
        hasPrevious_ = true;
        previous_ = (i32)(codepoint);
        previousSimple_ = simple;
        state_ = 0;
    }

    // True when the next breakBefore of a simple codepoint is guaranteed to
    // report a boundary through the fast path.
    [[gnu::always_inline]] bool simpleBoundary() const {
        return !hasPrevious_ || previousSimple_;
    }

    [[gnu::always_inline]] void reset() {
        hasPrevious_ = false;
        previous_ = 0;
        previousSimple_ = false;
        state_ = 0;
    }

private:
    bool breakBeforeSlow(u32 codepoint, bool simple);

    bool hasPrevious_ = false;
    bool previousSimple_ = false;
    i32 previous_ = 0;
    i32 state_ = 0;
};
