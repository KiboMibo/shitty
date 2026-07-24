/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

enum class PointerIcon {
    Text,
    Link
};

struct DesktopActions {
    virtual void openUri(stl::StringView uri) = 0;
    virtual void pointerIcon(PointerIcon icon) = 0;
};
