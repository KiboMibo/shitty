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

#include <cstdint>
#include <functional>

constexpr const u16 Missing_Glyph_Marker = 0x0000;

constexpr const u16 Unicode_Replacement_Character = 0xfffd;

struct Utf8Encoder {
    template <typename Fn>
    static void pushUnicode(u32 cp, Fn&& byteSink) {
        if (cp < 0x80) {
            byteSink(cp);
        } else if (cp < 0x0800) {
            byteSink((cp >> 6) | 0xc0);
            byteSink((cp & 0x3f) | 0x80);
        } else if (cp < 0x10000) {
            byteSink((cp >> 12) | 0xe0);
            byteSink(((cp >> 6) & 0x3f) | 0x80);
            byteSink((cp & 0x3f) | 0x80);
        } else {
            byteSink((cp >> 18) | 0xf0);
            byteSink(((cp >> 12) & 0x3f) | 0x80);
            byteSink(((cp >> 6) & 0x3f) | 0x80);
            byteSink((cp & 0x3f) | 0x80);
        }
    }
};

class Utf8Decoder {
public:
    using CodepointSink = std::function<void()>;

    Utf8Decoder(CodepointSink&& fn)
        : cpSink(fn)
    {
    }

    void checkPrematureEOS() {
        if (remaining > 0) {
            remaining = 0;
            emitReplacement();
        }
    }

    u32 getUnicode() const {
        return unicode;
    }

    bool expectsContinuation() const {
        return remaining != 0;
    }

    void setUnicode(u32 cp) {
        unicode = cp;
    }

    void onUnicode(u32 ch) {
        if (!ch) {
            return;
        }

        unicode = ch;
        cpSink();
    }

    void pushByte(unsigned char ch) {
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

private:
    void emitReplacement() {
        unicode = Unicode_Replacement_Character;
        cpSink();
    }

    u32 unicode = 0;
    u32 minimum = 0;
    u8 remaining = 0;
    CodepointSink cpSink;
};
