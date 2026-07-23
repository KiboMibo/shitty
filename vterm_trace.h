/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>
#include <string>

struct Composer;

enum class VtermTraceString : u8 {
    Osc,
    Dcs,
    Apc,
    Pm,
    Sos
};

struct VtermTrace {
    virtual void text(const u8* data, size_t size) = 0;
    virtual void control(u8 ch) = 0;
    virtual void escapeBegin() = 0;
    virtual void escapeByte(u8 ch) = 0;
    virtual void escapeEnd() = 0;
    virtual void escapeCancel() = 0;
    virtual void csi(u8 finalByte, const std::string& privatePrefix, const std::string& intermediates, const u32* parameters, const unsigned char* separators, size_t parameterCount, bool hadParameters) = 0;
    virtual void stringBegin(VtermTraceString type) = 0;
    virtual void stringData(const u8* data, size_t size) = 0;
    virtual void stringEnd() = 0;
    virtual void stringCancel() = 0;
    virtual std::string drain() = 0;
    virtual void clear() = 0;

    static VtermTrace* create(Composer& composer);
};
