/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct TerminalUpdate;

namespace plt {
    struct RenderContext;
}

struct Renderer {
    virtual bool update(const TerminalUpdate& update) = 0;
    virtual bool repaint() = 0;

    static Renderer* create(Composer& composer, const plt::RenderContext& context);
};
