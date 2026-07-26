/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct Color {
    u8 red;
    u8 green;
    u8 blue;
    u8 _reserved = 0;

    bool operator==(const Color& rhs) const {
        return red == rhs.red && green == rhs.green && blue == rhs.blue;
    }

    constexpr u32 packed() const noexcept {
        return (u32)(red) | ((u32)(green) << 8) | ((u32)(blue) << 16);
    }
};

static_assert(sizeof(Color) == 4);
