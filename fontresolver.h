#pragma once

#include <string>

struct FontVariants {
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;
};

FontVariants resolveFontconfig(const std::string& fontname);
