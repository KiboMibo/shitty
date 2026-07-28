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

struct InputBinding {
    plt::InputKey key = plt::InputKey::Unknown;
    u16 modifiers = 0;
    u32 baseCodepoint = 0;
    u32 textCodepoint = 0;
};

struct InputBindings: public InputHandler {
    virtual void add(const InputBinding& binding, stl::IntrusiveList* listeners) = 0;

    static InputBindings* create(Composer& composer);
};
