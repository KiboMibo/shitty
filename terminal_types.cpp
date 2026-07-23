/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "terminal_types.h"

namespace stl {}

using namespace stl;

namespace {
    constexpr u32 colorPayloadMask = 0x00ffffff;

    u32 packColor(CellColor color, bool background) noexcept {
        u32 kind = static_cast<u32>(color.source());
        if (background && kind < 2) {
            kind ^= 1;
        }
        return (color.encoded() & colorPayloadMask) | (kind << 24);
    }

    CellColor unpackColor(u32 packed, bool background) noexcept {
        u32 kind = packed >> 24;
        if (background && kind < 2) {
            kind ^= 1;
        }
        return CellColor::fromEncoded((kind << 30) | (packed & colorPayloadMask));
    }
}

CellColor TerminalCell::foreground() const noexcept {
    return unpackColor((u32)(fg_payload) | ((u32)(fg_kind) << 24), false);
}

CellColor TerminalCell::background() const noexcept {
    return unpackColor((u32)(bg_payload) | ((u32)(bg_kind) << 24), true);
}

void TerminalCell::setForeground(CellColor color) noexcept {
    const u32 packed = packColor(color, false);
    fg_payload = packed & colorPayloadMask;
    fg_kind = packed >> 24;
}

void TerminalCell::setBackground(CellColor color) noexcept {
    const u32 packed = packColor(color, true);
    bg_payload = packed & colorPayloadMask;
    bg_kind = packed >> 24;
}

CellColor TerminalCell::inlineUnderlineColor() const noexcept {
    return hasExtra() ? CellColor::defaultForeground() : unpackColor(payload & 0x03ffffff, false);
}

void TerminalCell::setInlineUnderlineColor(CellColor color) noexcept {
    extended = 0;
    payload = packColor(color, false);
}

RenderCell::RenderCell()
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
    , protected_char(0)
    , drawn(0)
    , _reserved(0)
    , fg(opts.fg)
    , bg(opts.bg)
    , underline_color(opts.fg)
{
}

Color TerminalColors::resolve(CellColor color) const {
    switch (color.source()) {
        case CellColor::Source::DefaultForeground:
            return defaultForeground;
        case CellColor::Source::DefaultBackground:
            return defaultBackground;
        case CellColor::Source::Indexed:
            return palette[color.index()];
        case CellColor::Source::Direct:
            return color.color();
    }
    return {};
}

Color TerminalColors::resolveForeground(const TerminalCell& cell) const {
    const bool overrideAnsi = (specialModes & (1u << 5)) != 0;
    const CellColor foreground = cell.foreground();
    if (overrideAnsi || foreground.source() == CellColor::Source::DefaultForeground) {
        if ((specialModes & (1u << 2)) != 0 && cell.blink) {
            return special[2];
        }
        if ((specialModes & (1u << 0)) != 0 && cell.bold) {
            return special[0];
        }
        if ((specialModes & (1u << 1)) != 0 && cell.underlined()) {
            return special[1];
        }
        if ((specialModes & (1u << 4)) != 0 && cell.italic) {
            return special[4];
        }
    }
    return resolve(foreground);
}

Color TerminalColors::resolveBackground(const TerminalCell& cell) const {
    const bool overrideAnsi = (specialModes & (1u << 5)) != 0;
    const CellColor background = cell.background();
    if ((overrideAnsi || background.source() == CellColor::Source::DefaultBackground) && (specialModes & (1u << 3)) != 0 && cell.inverse) {
        return special[3];
    }
    return resolve(background);
}
