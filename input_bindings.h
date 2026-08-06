/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "input_handler.h"

#include <std/sys/types.h>

namespace stl {
    class IntrusiveList;
}

struct Composer;

enum class InputActions : u8 {
    Copy,
    Paste,
    PastePrimary,
    PageUp,
    PageDown,
    IncFontSize,
    DecFontSize,
    ResetFontSize,
    NewTab,
    CloseTab,
    PrevTab,
    NextTab,
    Clear,
    Count,
};

struct InputBindings: public InputHandler {
    virtual void add(InputActions action, stl::IntrusiveList* listeners) = 0;

    static InputBindings* create(Composer& composer);
};
