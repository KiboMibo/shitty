/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include <cstdint>

struct CodepointProperties {
    u8 width;
    bool simpleGrapheme;
};

CodepointProperties codepointProperties(u32 codepoint);
int codepointWidth(u32 codepoint);

enum class GraphemeWidthEffect {
    Unchanged,
    Wide,
    Narrow,
};

GraphemeWidthEffect graphemeWidthEffect(u32 previous, u32 codepoint);

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
        hasPrevious_ = true;
        previous_ = (i32)(codepoint);
        previousSimple_ = codepoint >= 0x20 && codepoint < 0x7f;
        state_ = 0;
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
