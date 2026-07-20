/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "font_pack.h"
#include "font_resolver.h"
#include "log.h"

Fontpack::Fontpack(const std::string& fontname,
                   const std::string& dwfontname) {
    logT << "Fontpack: fontname=" << fontname
         << "; dwfontname=" << dwfontname << std::endl;

    const FontVariants variants = resolveFontconfig(fontname);
    if (variants.regular.empty()) {
        logE << "No Regular variant of the requested font '" << fontname
             << "' could be identified." << std::endl;
        throw std::runtime_error(
            "No suitable files for '" + fontname + "' found!");
    }

    fontRegular = std::make_unique<Font>(variants.regular);
    px = fontRegular->getPx();
    py = fontRegular->getPy();

    try {
        if (!variants.bold.empty())
            fontBold = std::make_unique<Font>(
                variants.bold, *fontRegular, Font::Overlay);
    } catch (const std::runtime_error& error) {
        fontBold = nullptr;
        logW << "Failed to load bold variant: " << error.what() << std::endl;
    }
    try {
        if (!variants.italic.empty())
            fontItalic = std::make_unique<Font>(
                variants.italic, *fontRegular, Font::Overlay);
    } catch (const std::runtime_error& error) {
        fontItalic = nullptr;
        logW << "Failed to load italic variant: " << error.what() << std::endl;
    }
    try {
        if (!variants.boldItalic.empty())
            fontBoldItalic = std::make_unique<Font>(
                variants.boldItalic, *fontRegular, Font::Overlay);
    } catch (const std::runtime_error& error) {
        fontBoldItalic = nullptr;
        logW << "Failed to load boldItalic variant: "
             << error.what() << std::endl;
    }

    FontVariants doubleWidth;
    if (!dwfontname.empty()) doubleWidth = resolveFontconfig(dwfontname);
    try {
        if (!doubleWidth.regular.empty()) {
            fontDoubleWidth = std::make_unique<Font>(
                doubleWidth.regular, *fontRegular, Font::DoubleWidth);
        } else if (!dwfontname.empty()) {
            logW << "Failed to locate requested double-width font: "
                 << dwfontname << std::endl;
        }
    } catch (const std::runtime_error& error) {
        fontDoubleWidth = nullptr;
        logW << "Failed to load double-width font: "
             << error.what() << std::endl;
    }
}
