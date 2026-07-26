/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include <cstdint>

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
        if (!hasPrevious_) {
            hasPrevious_ = true;
            previous_ = (i32)(codepoint);
            return true;
        }

        if (previous_ >= 0x20 && previous_ < 0x7f && codepoint >= 0x20 && codepoint < 0x7f) {
            previous_ = (i32)(codepoint);
            state_ = 0;
            return true;
        }

        return breakBeforeSlow(codepoint);
    }

    [[gnu::always_inline]] void setBoundaryAfter(u32 codepoint) {
        hasPrevious_ = true;
        previous_ = (i32)(codepoint);
        state_ = 0;
    }

    [[gnu::always_inline]] void reset() {
        hasPrevious_ = false;
        previous_ = 0;
        state_ = 0;
    }

private:
    bool breakBeforeSlow(u32 codepoint);

    bool hasPrevious_ = false;
    i32 previous_ = 0;
    i32 state_ = 0;
};
