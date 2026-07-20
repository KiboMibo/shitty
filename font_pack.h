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

#include "font.h"

#include <cstdint>
#include <string>

struct Composer;

struct Fontpack {
    virtual uint16_t getPx() const = 0;
    virtual uint16_t getPy() const = 0;

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

    static Fontpack* create(Composer& composer,
                            const std::string& fontname,
                            const std::string& dwfontname);
};
