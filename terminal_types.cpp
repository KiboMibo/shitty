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

#include "terminal_types.h"

TerminalCell::TerminalCell()
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
{
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
