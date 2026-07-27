/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once
#include <std/sys/types.h>
#include <std/str/view.h>

#include <string>

struct VtermWindowInfo {
    i32 x = 0;
    i32 y = 0;
    u32 screenPixelWidth = 0;
    u32 screenPixelHeight = 0;
    bool iconified = false;
    bool maximized = false;
    bool fullscreen = false;
};

struct VtermHost {
    virtual void osc(int command, const std::string& argument) = 0;
    virtual bool handlesOsc() const = 0;
    virtual void title(stl::StringView title) = 0;
    virtual void cwd(stl::StringView path) = 0;
    virtual void bell() = 0;
    virtual bool handlesPrinter() const = 0;
    virtual void print(stl::StringView output) = 0;
    virtual void leds(u8 state) = 0;
    virtual void notify(const std::string& id, const std::string& title, const std::string& body, bool close) = 0;
    virtual void progress(u32 state, u32 percent) = 0;
    virtual void windowOperation(u32 operation, u32 first, u32 second) = 0;
    virtual VtermWindowInfo windowInfo() = 0;
};
