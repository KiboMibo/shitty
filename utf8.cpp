/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Zutty.
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

Utf8Decoder::Utf8Decoder(CodepointSink&& fn)
    : cpSink(fn)
{
}

void Utf8Decoder::checkPrematureEOS() {
    if (remaining > 0) {
        remaining = 0;
        emitReplacement();
    }
}

void Utf8Decoder::reset() {
    unicode = 0;
    minimum = 0;
    remaining = 0;
}

void Utf8Decoder::onUnicode(u32 ch) {
    if (!ch) {
        return;
    }

    unicode = ch;
    cpSink();
}

void Utf8Decoder::pushByte(unsigned char ch) {
    if ((ch & 0xc0) == 0x80) {
        if (remaining == 0) {
            emitReplacement();
            return;
        }
        unicode = (unicode << 6) | (ch & 0x3f);
        if (--remaining == 0) {
            if (unicode < minimum || unicode > 0x10ffff || (unicode >= 0xd800 && unicode <= 0xdfff)) {
                emitReplacement();
            } else {
                cpSink();
            }
        }
    } else if (ch >= 0xc2 && ch <= 0xdf) {
        checkPrematureEOS();
        unicode = ch & 0x1f;
        remaining = 1;
        minimum = 0x80;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        checkPrematureEOS();
        unicode = ch & 0x0f;
        remaining = 2;
        minimum = 0x800;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        checkPrematureEOS();
        unicode = ch & 0x07;
        remaining = 3;
        minimum = 0x10000;
    } else {
        checkPrematureEOS();
        emitReplacement();
    }
}

void Utf8Decoder::emitReplacement() {
    unicode = Unicode_Replacement_Character;
    cpSink();
}
