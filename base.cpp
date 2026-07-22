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

#include "base.h"

#include <iomanip>
#include <ostream>


namespace stl {}
using namespace stl;

std::ostream& operator<<(std::ostream& os, const Color& c) {
    os << "rgb:" << std::hex << std::setfill('0') << std::setw(2) << (int)c.red << std::setw(2) << (int)c.red << "/" << std::setw(2) << (int)c.green << std::setw(2) << (int)c.green << "/" << std::setw(2) << (int)c.blue << std::setw(2) << (int)c.blue;
    return os;
}

Point::Point(int x_, int y_)
    : x(x_)
    , y(y_)
{
}

std::ostream& operator<<(std::ostream& os, const Point& p) {
    os << "(" << p.x << "," << p.y << ")";
    return os;
}

Rect::Rect(Point tl_, Point br_)
    : tl(tl_)
    , br(br_)
{
}

Rect::Rect(int x, int y)
    : tl(x, y)
    , br(x + 1, y)
{
}

Rect::Rect(int x1, int y1, int x2, int y2)
    : tl(x1, y1)
    , br(x2, y2)
{
}

void Rect::clear() {
    tl = Point();
    br = Point();
}

std::ostream& operator<<(std::ostream& os, const Rect& r) {
    os << "Rect{tl=" << r.tl << " " << "br=" << r.br;
    if (r.rectangular) {
        os << " rectangular}";
    } else {
        os << " regular}";
    }
    return os;
}
