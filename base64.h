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
#include <string>
#include <vector>

namespace {
    static constexpr const char* syms =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static std::vector<int> rtab =
        [] {
        std::vector<int> rt(256, -1);
        for (int i = 0; i < 64; i++) {
            rt[syms[i]] = i;
        }
        return rt;
    }();
}

namespace base64 {

    static std::string
    encode(const std::string& in) {
        std::string out;
        out.reserve(in.size() * 4 / 3 + 3);

        int val = 0;
        int valb = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(syms[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            out.push_back(syms[((val << 8) >> (valb + 8)) & 0x3F]);
        }
        while (out.size() % 4) {
            out.push_back('=');
        }
        return out;
    }

    static bool
    decode(const std::string& in, std::string& out) {
        out.clear();
        out.reserve(in.size() * 3 / 4);

        size_t dataSize = in.size();
        size_t padding = 0;
        while (dataSize > 0 && in[dataSize - 1] == '=') {
            --dataSize;
            ++padding;
        }
        if (padding > 2 || (padding && in.size() % 4 != 0) ||
            dataSize % 4 == 1) {
            return false;
        }
        for (size_t k = 0; k < dataSize; ++k) {
            if (rtab[(unsigned char)(in[k])] < 0) {
                return false;
            }
        }
        for (size_t k = dataSize; k < in.size(); ++k) {
            if (in[k] != '=') {
                return false;
            }
        }
        if ((padding == 1 && dataSize % 4 != 3) ||
            (padding == 2 && dataSize % 4 != 2)) {
            return false;
        }

        u32 value = 0;
        int bits = 0;
        for (size_t k = 0; k < dataSize; ++k) {
            value = (value << 6) |
                    (u32)(rtab[(unsigned char)(in[k])]);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back((char)((value >> bits) & 0xff));
                value &= bits == 0 ? 0 : (u32{1} << bits) - 1;
            }
        }
        if (value != 0) {
            out.clear();
            return false;
        }
        return true;
    }

}
