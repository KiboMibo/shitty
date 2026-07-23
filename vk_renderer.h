/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct GLFWwindow;

class Frame;

struct Renderer {
    virtual bool update(const Frame& frame) = 0;
    virtual bool repaint() = 0;

    static Renderer* create(Composer& composer, GLFWwindow* window);
};
