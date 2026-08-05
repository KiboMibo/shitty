/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "color.h"

#include <cstddef>

struct AnsiPalette {
    static constexpr size_t colorCount = 16;

    Color colors[colorCount]{};

    constexpr Color& operator[](size_t index) noexcept {
        return colors[index];
    }

    constexpr const Color& operator[](size_t index) const noexcept {
        return colors[index];
    }

    bool operator==(const AnsiPalette& rhs) const noexcept;
};
