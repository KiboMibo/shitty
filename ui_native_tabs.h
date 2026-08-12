/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class ObjPool;
}

struct Composer;
struct Ui;

// The raw terminal surface plus a projection of the session set into
// the platform's native title bar, where one exists: Cocoa shows a tab
// strip from the second session on, Wayland and headless show nothing
// and the Ui degrades to the bare terminal.
Ui* createNativeTabsUi(stl::ObjPool& owner, Composer& composer);
