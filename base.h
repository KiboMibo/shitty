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

#include <cstdint>
#include <iosfwd>

struct Color {
    u8 red;
    u8 green;
    u8 blue;

    bool operator==(const Color& rhs) const {
        return red == rhs.red && green == rhs.green && blue == rhs.blue;
    }
};

std::ostream& operator<<(std::ostream& os, const Color& c);

struct Point {
    int x = -1;
    int y = -1;

    Point() = default;
    Point(int x_, int y_);

    bool operator<(const Point& rhs) const {
        return y < rhs.y || (y == rhs.y && x < rhs.x);
    }

    bool operator==(const Point& rhs) const {
        return x == rhs.x && y == rhs.y;
    }

    bool operator<=(const Point& rhs) const {
        return operator<(rhs) || operator==(rhs);
    }
};

std::ostream& operator<<(std::ostream& os, const Point& p);

struct Rect {
    Point tl;
    Point br;
    bool rectangular = false;

    Rect() = default;
    Rect(Point tl_, Point br_);
    Rect(int x, int y);
    Rect(int x1, int y1, int x2, int y2);

    bool null() const {
        return tl == Point() && br == Point();
    }

    bool empty() const {
        return tl == br;
    }

    Point mid() const {
        return Point((tl.x + br.x) / 2, (tl.y + br.y) / 2);
    }

    void clear();

    void toggleRectangular() {
        rectangular = !rectangular;
    }
};

std::ostream& operator<<(std::ostream& os, const Rect& r);
