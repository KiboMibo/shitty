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
    bool breakBefore(u32 codepoint);
    void setBoundaryAfter(u32 codepoint);
    void reset();

private:
    bool hasPrevious_ = false;
    i32 previous_ = 0;
    i32 state_ = 0;
};
