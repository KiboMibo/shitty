/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

struct Clipboard {
    virtual stl::StringView readPrimary() = 0;
    virtual stl::StringView readClipboard() = 0;
    virtual void writePrimary(stl::StringView content) = 0;
    virtual void writeClipboard(stl::StringView content) = 0;
};
