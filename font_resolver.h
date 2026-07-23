/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <string>

struct FontVariants {
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;
};

FontVariants resolveFontconfig(const std::string& fontname);
void finalizeFontconfig() noexcept;
