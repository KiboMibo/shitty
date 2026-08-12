/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct Color;
struct Composer;
struct Ui;

struct Brand {
    virtual stl::StringView displayName() const = 0;
    virtual stl::StringView executableName() const = 0;
    virtual stl::StringView identifier() const = 0;
    virtual stl::StringView fontSizeEnvironment() const = 0;
    virtual stl::StringView versionEnvironment() const = 0;
    virtual stl::StringView iconData() const = 0;
    // The logo color the "default" color scheme leans toward, and the
    // compile-time position of its tint slider; -tint moves it at runtime.
    virtual Color accentColor() const = 0;
    virtual double accentTint() const = 0;
    // The window chrome this personality surrounds the terminal with on
    // this platform. The Ui and everything it registers live in owner.
    virtual Ui* createUi(stl::ObjPool& owner, Composer& composer) const = 0;

    const char* identifierCString() const;
    void configureVersionEnvironment() const;

    // Headless/unit-test composers use a neutral brand unless their adapter
    // supplies the production brand explicitly.
    static Brand* generic();
};

int runMain(Brand& brand, int argc, char* argv[]);
