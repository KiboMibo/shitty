#pragma once

#include <string>

struct FontVariants {
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;
};

FontVariants resolveFontTree(
    const std::string& fontpath, const std::string& fontname);

FontVariants resolveFontconfig(const std::string& fontname);
