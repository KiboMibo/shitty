/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "fontpack.h"
#include "fontresolver.h"
#include "log.h"

#include <fontconfig/fontconfig.h>

namespace {
    std::string fcFindFile(
        const std::string& family, int weight, int slant) {
        FcPattern* pattern = FcPatternCreate();
        if (!pattern) return {};
        FcPatternAddString(
            pattern, FC_FAMILY,
            reinterpret_cast<const FcChar8*>(family.c_str()));
        FcPatternAddInteger(pattern, FC_WEIGHT, weight);
        FcPatternAddInteger(pattern, FC_SLANT, slant);
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult result;
        FcPattern* match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        if (!match) return {};

        std::string path;
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
            path = reinterpret_cast<const char*>(file);
        FcPatternDestroy(match);
        return path;
    }

    FontVariants fcFindVariants(const std::string& family) {
        FontVariants variants;
        variants.regular = fcFindFile(
            family, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
        if (variants.regular.empty()) return variants;

        logI << "fontconfig resolved '" << family << "' to "
             << variants.regular << std::endl;
        std::string path = fcFindFile(
            family, FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
        if (!path.empty() && path != variants.regular) variants.bold = path;
        path = fcFindFile(family, FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
        if (!path.empty() && path != variants.regular) variants.italic = path;
        path = fcFindFile(family, FC_WEIGHT_BOLD, FC_SLANT_ITALIC);
        if (!path.empty() && path != variants.regular &&
            path != variants.bold && path != variants.italic)
            variants.boldItalic = path;
        return variants;
    }
}

Fontpack::Fontpack(const std::string& fontpath,
                   const std::string& fontname,
                   const std::string& dwfontname) {
    logT << "Fontpack: fontpath=" << fontpath
         << "; fontname=" << fontname
         << "; dwfontname=" << dwfontname << std::endl;

    FontVariants variants = resolveFontTree(fontpath, fontname);
    if (variants.regular.empty()) {
        logI << "No files matching '" << fontname << "' found under '"
             << fontpath << "'; trying fontconfig" << std::endl;
        variants = fcFindVariants(fontname);
    }
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

    FontVariants doubleWidth = resolveFontTree(fontpath, dwfontname);
    if (doubleWidth.regular.empty() && !dwfontname.empty()) {
        logI << "No files matching '" << dwfontname << "' found under '"
             << fontpath << "'; trying fontconfig" << std::endl;
        doubleWidth.regular = fcFindFile(
            dwfontname, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
    }
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
