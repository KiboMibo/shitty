/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct Composer;
struct TerminalCell;
struct TerminalUpdate;

namespace plt {
    struct RenderContext;
}

struct Renderer {
    virtual bool update(const TerminalUpdate& update) = 0;
    virtual bool repaint() = 0;

    static Renderer* create(Composer& composer, const plt::RenderContext& context);

    static u32 cellAttributes(const TerminalCell& cell);
};
