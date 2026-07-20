/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once

#include "base.h"
#include "options.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

struct TerminalCell {
    uint32_t uc_pt = ' ';
    uint8_t dwidth : 1;
    uint8_t dwidth_cont : 1;
    uint8_t bold : 1;
    uint8_t italic : 1;
    uint8_t underline : 1;
    uint8_t inverse : 1;
    uint8_t wrap : 1;
    uint8_t dirty : 1;
    uint8_t faint : 1;
    uint8_t blink : 1;
    uint8_t conceal : 1;
    uint8_t strike : 1;
    uint8_t overline : 1;
    uint8_t underline_style : 3;
    uint8_t protected_char = 0;
    uint8_t line_attr = 0;
    Color fg;
    uint8_t _fill1;
    Color bg;
    uint8_t _fill2;
    Color underline_color;
    uint8_t _fill3;
    uint32_t hyperlink = 0;
    uint32_t grapheme = 0;
    int32_t fg_index = -2;
    int32_t bg_index = -2;
    int32_t underline_index = -2;
    uint32_t semantic = 0;
    uint32_t line_attribute = 0;

    TerminalCell()
        : dwidth(0)
        , dwidth_cont(0)
        , bold(0)
        , italic(0)
        , underline(0)
        , inverse(0)
        , wrap(0)
        , dirty(0)
        , faint(0)
        , blink(0)
        , conceal(0)
        , strike(0)
        , overline(0)
        , underline_style(0)
        , fg(opts.fg)
        , bg(opts.bg)
        , underline_color(opts.fg)
    {
    }

    using Ptr = std::shared_ptr<TerminalCell>;

    bool operator==(const TerminalCell& rhs) const {
        return memcmp(this, &rhs, sizeof(TerminalCell)) == 0;
    }

    bool operator!=(const TerminalCell& rhs) const {
        return !operator==(rhs);
    }

    static Ptr make(uint16_t columns, uint16_t rows) {
        return Ptr(new TerminalCell[rows * columns],
                   std::default_delete<TerminalCell[]>());
    }
};

static_assert(sizeof(TerminalCell) == 48, "TerminalCell size mismatch");
static_assert(offsetof(TerminalCell, uc_pt) == 0,
              "TerminalCell codepoint offset mismatch");
static_assert(offsetof(TerminalCell, fg) == 8,
              "TerminalCell foreground offset mismatch");
static_assert(offsetof(TerminalCell, bg) == 12,
              "TerminalCell background offset mismatch");
static_assert(offsetof(TerminalCell, underline_color) == 16,
              "TerminalCell underline offset mismatch");
static_assert(offsetof(TerminalCell, hyperlink) == 20,
              "TerminalCell hyperlink offset mismatch");
static_assert(offsetof(TerminalCell, grapheme) == 24,
              "TerminalCell grapheme offset mismatch");
static_assert(offsetof(TerminalCell, fg_index) == 28,
              "TerminalCell foreground source offset mismatch");

struct TerminalCursor {
    Color color = opts.cr;
    uint16_t posX = 0;
    uint16_t posY = 0;

    enum class Style : uint8_t {
        hidden = 0,
        filled_block = 1,
        hollow_block = 2,
        underline = 3,
        bar = 4
    };
    Style style = Style::hidden;
};
