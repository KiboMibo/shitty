/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

#include <std/sys/types.h>

#include <cstdint>
#include <string>

struct Composer;

struct Fontpack {
    virtual u16 getPx() const = 0;
    virtual u16 getPy() const = 0;

    virtual const Font& getRegular() const = 0;
    virtual bool hasBold() const = 0;
    virtual const Font& getBold() const = 0;
    virtual bool hasItalic() const = 0;
    virtual const Font& getItalic() const = 0;
    virtual bool hasBoldItalic() const = 0;
    virtual const Font& getBoldItalic() const = 0;
    virtual bool hasDoubleWidth() const = 0;
    virtual const Font& getDoubleWidth() const = 0;

    // Once the renderer has uploaded all atlases, host memory can go away.
    virtual void releaseFonts() = 0;

    static Fontpack* create(Composer& composer, const std::string& fontname, const std::string& dwfontname);
};
