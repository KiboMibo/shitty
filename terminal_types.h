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
#include <std/sys/types.h>

#include "base.h"
#include "options.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

struct TerminalCell {
    u32 uc_pt = ' ';
    u8 dwidth : 1;
    u8 dwidth_cont : 1;
    u8 bold : 1;
    u8 italic : 1;
    u8 underline : 1;
    u8 inverse : 1;
    u8 wrap : 1;
    u8 dirty : 1;
    u8 faint : 1;
    u8 blink : 1;
    u8 conceal : 1;
    u8 strike : 1;
    u8 overline : 1;
    u8 underline_style : 3;
    u8 protected_char = 0;
    u8 line_attr = 0;
    Color fg;
    u8 _fill1;
    Color bg;
    u8 _fill2;
    Color underline_color;
    u8 _fill3;
    u32 hyperlink = 0;
    u32 grapheme = 0;
    i32 fg_index = -2;
    i32 bg_index = -2;
    i32 underline_index = -2;
    u32 semantic = 0;
    u32 line_attribute = 0;

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

    static Ptr make(u16 columns, u16 rows) {
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
    u16 posX = 0;
    u16 posY = 0;

    enum class Style : u8 {
        hidden = 0,
        filled_block = 1,
        hollow_block = 2,
        underline = 3,
        bar = 4
    };
    Style style = Style::hidden;
};
