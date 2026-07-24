/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;
struct Renderer;
struct TestModeInput;

struct WindowEvents {
    bool close = false;
    bool resized = false;
    bool redraw = false;
};

struct WindowInfo {
    i32 x = 0;
    i32 y = 0;
    u32 screenPixelWidth = 0;
    u32 screenPixelHeight = 0;
    bool iconified = false;
    bool maximized = false;
    bool fullscreen = false;
};

struct Window {
    virtual void initialize() = 0;
    virtual float density() = 0;
    virtual void show() = 0;
    virtual void activate() = 0;
    virtual WindowEvents dispatchEvents(double timeout) = 0;

    virtual void setTitle(stl::StringView title) = 0;
    virtual void requestAttention() = 0;
    virtual void requestRedraw() = 0;
    virtual void restore() = 0;
    virtual void iconify() = 0;
    virtual void move(i32 x, i32 y) = 0;
    virtual void focus() = 0;
    virtual void setMaximized(bool maximized) = 0;
    virtual void setFullscreen(bool fullscreen) = 0;
    virtual void resizePixels(u32 width, u32 height) = 0;
    virtual WindowInfo info() = 0;

    virtual Renderer* createRender() = 0;
    virtual TestModeInput* testApi() = 0;

    static Window* create(Composer& composer);
};
