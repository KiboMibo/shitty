/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Shitty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "font_pack.h"

#include "composer.h"
#include "font_resolver.h"
#include "log.h"

#include <std/mem/obj_pool.h>

#include <memory>
#include <stdexcept>


namespace stl {}
using namespace stl;

namespace {

    class FontpackImpl final: public Fontpack {
    public:
        FontpackImpl(const char* fontname, const char* dwfontname);

        u16 getPx() const override;
        u16 getPy() const override;

        const Font& getRegular() const override;
        bool hasBold() const override;
        const Font& getBold() const override;
        bool hasItalic() const override;
        const Font& getItalic() const override;
        bool hasBoldItalic() const override;
        const Font& getBoldItalic() const override;
        bool hasDoubleWidth() const override;
        const Font& getDoubleWidth() const override;
        void releaseFonts() override;

    private:
        FontpackImpl(const std::string& fontname, const std::string& dwfontname);

        u16 px = 0;
        u16 py = 0;
        std::unique_ptr<Font> fontRegular;
        std::unique_ptr<Font> fontBold;
        std::unique_ptr<Font> fontItalic;
        std::unique_ptr<Font> fontBoldItalic;
        std::unique_ptr<Font> fontDoubleWidth;
    };

}

FontpackImpl::FontpackImpl(const char* fontname_, const char* dwfontname_)
    : FontpackImpl(std::string(fontname_), std::string(dwfontname_))
{
}

u16 FontpackImpl::getPx() const {
    return px;
}

u16 FontpackImpl::getPy() const {
    return py;
}

const Font& FontpackImpl::getRegular() const {
    return *fontRegular;
}

bool FontpackImpl::hasBold() const {
    return fontBold != nullptr;
}

bool FontpackImpl::hasItalic() const {
    return fontItalic != nullptr;
}

bool FontpackImpl::hasBoldItalic() const {
    return fontBoldItalic != nullptr;
}

bool FontpackImpl::hasDoubleWidth() const {
    return fontDoubleWidth != nullptr;
}

FontpackImpl::FontpackImpl(const std::string& fontname, const std::string& dwfontname) {
    logT << "Fontpack: fontname=" << fontname << "; dwfontname=" << dwfontname << std::endl;

    const FontVariants variants = resolveFontconfig(fontname);
    if (variants.regular.empty()) {
        logE << "No Regular variant of the requested font '" << fontname << "' could be identified." << std::endl;
        throw std::runtime_error("No suitable files for '" + fontname + "' found!");
    }

    fontRegular = std::make_unique<Font>(variants.regular);
    px = fontRegular->getPx();
    py = fontRegular->getPy();

    try {
        if (!variants.bold.empty()) {
            fontBold = std::make_unique<Font>(variants.bold, *fontRegular, Font::Overlay);
        }
    } catch (const std::runtime_error& error) {
        fontBold = nullptr;
        logW << "Failed to load bold variant: " << error.what() << std::endl;
    }
    try {
        if (!variants.italic.empty()) {
            fontItalic = std::make_unique<Font>(variants.italic, *fontRegular, Font::Overlay);
        }
    } catch (const std::runtime_error& error) {
        fontItalic = nullptr;
        logW << "Failed to load italic variant: " << error.what() << std::endl;
    }
    try {
        if (!variants.boldItalic.empty()) {
            fontBoldItalic = std::make_unique<Font>(variants.boldItalic, *fontRegular, Font::Overlay);
        }
    } catch (const std::runtime_error& error) {
        fontBoldItalic = nullptr;
        logW << "Failed to load boldItalic variant: " << error.what() << std::endl;
    }

    FontVariants doubleWidth;
    if (!dwfontname.empty()) {
        doubleWidth = resolveFontconfig(dwfontname);
    }
    try {
        if (!doubleWidth.regular.empty()) {
            fontDoubleWidth = std::make_unique<Font>(doubleWidth.regular, *fontRegular, Font::DoubleWidth);
        } else if (!dwfontname.empty()) {
            logW << "Failed to locate requested double-width font: " << dwfontname << std::endl;
        }
    } catch (const std::runtime_error& error) {
        fontDoubleWidth = nullptr;
        logW << "Failed to load double-width font: " << error.what() << std::endl;
    }
}

const Font& FontpackImpl::getBold() const {
    if (!hasBold()) {
        throw std::runtime_error("No Bold font variant present!");
    }
    return *fontBold;
}

const Font& FontpackImpl::getItalic() const {
    if (!hasItalic()) {
        throw std::runtime_error("No Italic font variant present!");
    }
    return *fontItalic;
}

const Font& FontpackImpl::getBoldItalic() const {
    if (!hasBoldItalic()) {
        throw std::runtime_error("No BoldItalic font variant present!");
    }
    return *fontBoldItalic;
}

const Font& FontpackImpl::getDoubleWidth() const {
    if (!hasDoubleWidth()) {
        throw std::runtime_error("No DoubleWidth font present!");
    }
    return *fontDoubleWidth;
}

void FontpackImpl::releaseFonts() {
    fontRegular = nullptr;
    fontBold = nullptr;
    fontItalic = nullptr;
    fontBoldItalic = nullptr;
    fontDoubleWidth = nullptr;
}

Fontpack* Fontpack::create(Composer& composer, const std::string& fontname, const std::string& dwfontname) {
    return composer.pool->make<FontpackImpl>(fontname.c_str(), dwfontname.c_str());
}
