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

#include "utf8.h"

namespace stl {}

using namespace stl;

bool Utf8Decoder::checkPrematureEOS() {
    if (remaining > 0) {
        remaining = 0;
        unicode = Unicode_Replacement_Character;
        return true;
    }
    return false;
}

void Utf8Decoder::reset() {
    accumulator = 0;
    unicode = 0;
    minimum = 0;
    remaining = 0;
}

bool Utf8Decoder::onUnicode(u32 ch) {
    if (!ch) {
        return false;
    }

    unicode = ch;
    return true;
}

int Utf8Decoder::pushByte(unsigned char ch) {
    if ((ch & 0xc0) == 0x80) {
        if (remaining == 0) {
            unicode = Unicode_Replacement_Character;
            return 1;
        }
        accumulator = (accumulator << 6) | (ch & 0x3f);
        if (--remaining != 0) {
            return 0;
        }
        if (accumulator < minimum || accumulator > 0x10ffff || (accumulator >= 0xd800 && accumulator <= 0xdfff)) {
            unicode = Unicode_Replacement_Character;
        } else {
            unicode = accumulator;
        }
        return 1;
    }

    int completed = 0;
    if (remaining > 0) {
        remaining = 0;
        unicode = Unicode_Replacement_Character;
        completed = 1;
    }
    if (ch >= 0xc2 && ch <= 0xdf) {
        accumulator = ch & 0x1f;
        remaining = 1;
        minimum = 0x80;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        accumulator = ch & 0x0f;
        remaining = 2;
        minimum = 0x800;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        accumulator = ch & 0x07;
        remaining = 3;
        minimum = 0x10000;
    } else {
        unicode = Unicode_Replacement_Character;
        ++completed;
    }
    return completed;
}
