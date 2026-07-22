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

class CellColor {
public:
    enum class Source : u32 {
        DefaultForeground = 0,
        DefaultBackground = 1,
        Indexed = 2,
        Direct = 3,
    };

    constexpr CellColor() = default;

    static constexpr CellColor defaultForeground() {
        return CellColor(Source::DefaultForeground, 0);
    }

    static constexpr CellColor defaultBackground() {
        return CellColor(Source::DefaultBackground, 0);
    }

    static constexpr CellColor indexed(u8 index) {
        return CellColor(Source::Indexed, index);
    }

    static constexpr CellColor direct(Color color) {
        return CellColor(Source::Direct, (u32)(color.red) | ((u32)(color.green) << 8) | ((u32)(color.blue) << 16));
    }

    constexpr Source source() const {
        return (Source)(value >> sourceShift);
    }

    constexpr u8 index() const {
        return (u8)(value & 0xff);
    }

    constexpr Color color() const {
        return {
            (u8)(value & 0xff),
            (u8)((value >> 8) & 0xff),
            (u8)((value >> 16) & 0xff),
        };
    }

    constexpr i32 legacyIndex() const {
        return source() == Source::Indexed ? index() : source() == Source::Direct ? -1 : -2;
    }

    constexpr bool operator==(CellColor rhs) const {
        return value == rhs.value;
    }

private:
    static constexpr u32 sourceShift = 30;
    static constexpr u32 payloadMask = 0x00ffffff;

    constexpr CellColor(Source source, u32 payload)
        : value(((u32)(source) << sourceShift) | (payload & payloadMask))
    {
    }

    u32 value = 0;
};

static_assert(sizeof(CellColor) == 4, "CellColor size mismatch");

struct TerminalColors {
    Color palette[256]{};
    Color defaultForeground{};
    Color defaultBackground{};

    Color resolve(CellColor color) const;
};

struct TerminalCell {
    static constexpr u8 decProtection = 1;
    static constexpr u8 isoProtection = 2;

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
    CellColor fg = CellColor::defaultForeground();
    CellColor bg = CellColor::defaultBackground();
    CellColor underline_color = CellColor::defaultForeground();
    u32 hyperlink = 0;
    u32 grapheme = 0;
    u32 semantic = 0;

    TerminalCell();

    using Ptr = std::shared_ptr<TerminalCell>;

    bool operator==(const TerminalCell& rhs) const {
        return memcmp(this, &rhs, sizeof(TerminalCell)) == 0;
    }

    bool operator!=(const TerminalCell& rhs) const {
        return !operator==(rhs);
    }

    static Ptr make(u16 columns, u16 rows) {
        return Ptr(new TerminalCell[rows * columns], std::default_delete<TerminalCell[]>());
    }
};

static_assert(sizeof(TerminalCell) == 32, "TerminalCell size mismatch");
static_assert(offsetof(TerminalCell, uc_pt) == 0, "TerminalCell codepoint offset mismatch");
static_assert(offsetof(TerminalCell, fg) == 8, "TerminalCell foreground offset mismatch");
static_assert(offsetof(TerminalCell, bg) == 12, "TerminalCell background offset mismatch");
static_assert(offsetof(TerminalCell, underline_color) == 16, "TerminalCell underline offset mismatch");
static_assert(offsetof(TerminalCell, hyperlink) == 20, "TerminalCell hyperlink offset mismatch");
static_assert(offsetof(TerminalCell, grapheme) == 24, "TerminalCell grapheme offset mismatch");
static_assert(offsetof(TerminalCell, semantic) == 28, "TerminalCell semantic offset mismatch");

struct RenderCell {
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
    u8 _fill1 = 0;
    Color bg;
    u8 _fill2 = 0;
    Color underline_color;
    u8 _fill3 = 0;
    u32 hyperlink = 0;
    u32 grapheme = 0;
    u32 semantic = 0;

    RenderCell();

    bool operator==(const RenderCell& rhs) const {
        return memcmp(this, &rhs, sizeof(RenderCell)) == 0;
    }

    bool operator!=(const RenderCell& rhs) const {
        return !operator==(rhs);
    }
};

static_assert(sizeof(RenderCell) == 32, "RenderCell size mismatch");
static_assert(offsetof(RenderCell, fg) == 8, "RenderCell foreground offset mismatch");

struct TerminalPen {
    TerminalCell cell;
    Color fg;
    Color bg;
};

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
