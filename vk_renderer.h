/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#ifdef SHITTY_FOR_TESTS
    #include <std/sys/types.h>
#endif

struct Composer;
struct GLFWwindow;

class Frame;

struct Renderer {
    virtual bool update(const Frame& frame) = 0;
    virtual bool repaint() = 0;

    static Renderer* create(Composer& composer, GLFWwindow* window);
};

#ifdef SHITTY_FOR_TESTS
struct RenderCell;

u32 rendererCellAttributesForTest(const RenderCell& cell);
#endif
