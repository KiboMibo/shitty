/* This file is part of Shitty.
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

    constexpr bool operator!=(CellColor rhs) const {
        return value != rhs.value;
    }

    constexpr u32 encoded() const {
        return value;
    }

    static constexpr CellColor fromEncoded(u32 encoded) {
        CellColor result;
        result.value = encoded;
        return result;
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

struct TerminalCell;

struct TerminalColors {
    static constexpr u8 specialCount = 5;

    Color palette[256]{};
    Color defaultForeground{};
    Color defaultBackground{};
    Color special[specialCount]{};
    Color originalSpecial[specialCount]{};
    u8 specialModes = 0;

    Color resolve(CellColor color) const;
    Color resolveForeground(const TerminalCell& cell) const;
    Color resolveBackground(const TerminalCell& cell) const;
};

struct TerminalCell {
    static constexpr u8 decProtection = 1;
    static constexpr u8 isoProtection = 2;

    union {
        u64 style;
        struct {
            u64 fg_payload : 24;
            u64 fg_kind : 2;
            u64 bg_payload : 24;
            u64 bg_kind : 2;
            u64 bold : 1;
            u64 italic : 1;
            u64 faint : 1;
            u64 blink : 1;
            u64 conceal : 1;
            u64 inverse : 1;
            u64 strike : 1;
            u64 overline : 1;
            u64 underline_style : 3;
            u64 drawn : 1;
        };
    };

    union {
        u32 content;
        struct {
            u32 uc_pt : 21;
            u32 dwidth : 1;
            u32 dwidth_cont : 1;
            u32 protected_char : 2;
            u32 semantic : 2;
            u32 wrap : 1;
            u32 dirty : 1;
            u32 line_attr : 2;
            u32 extended : 1;
        };
    };

    u32 payload;

    constexpr bool underlined() const noexcept {
        return underline_style != 0;
    }

    CellColor foreground() const noexcept;
    CellColor background() const noexcept;
    void setForeground(CellColor color) noexcept;
    void setBackground(CellColor color) noexcept;

    CellColor inlineUnderlineColor() const noexcept;
    void setInlineUnderlineColor(CellColor color) noexcept;

    constexpr bool hasExtra() const noexcept {
        return extended != 0;
    }

    constexpr u32 extraRef() const noexcept {
        return hasExtra() ? payload : 0;
    }

    void setExtraRef(u32 ref) noexcept {
        extended = ref != 0;
        payload = ref;
    }

    using Ptr = std::shared_ptr<TerminalCell>;

    bool operator==(const TerminalCell& rhs) const {
        return style == rhs.style && content == rhs.content && payload == rhs.payload;
    }

    bool operator!=(const TerminalCell& rhs) const {
        return !operator==(rhs);
    }

    static Ptr make(u16 columns, u16 rows) {
        return Ptr(new TerminalCell[rows * columns](), std::default_delete<TerminalCell[]>());
    }
};

static_assert(sizeof(TerminalCell) == 16, "TerminalCell size mismatch");
static_assert(offsetof(TerminalCell, content) == 8, "TerminalCell content offset mismatch");
static_assert(offsetof(TerminalCell, payload) == 12, "TerminalCell payload offset mismatch");
static_assert(__is_trivial(TerminalCell), "TerminalCell must remain trivial");
static_assert(__is_trivially_copyable(TerminalCell), "TerminalCell must remain trivially copyable");
static_assert(__is_standard_layout(TerminalCell), "TerminalCell must remain standard-layout");

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
    u8 protected_char : 2;
    u8 drawn : 1;
    u8 _reserved : 5;
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
    TerminalCell cell{};
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
