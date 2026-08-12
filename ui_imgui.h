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

// Imtty's chrome: the whole window is one docking-style ImGui window
// whose header carries the session tabs; the terminal grid lives in a
// composited layer under the header. On a backend without the ImGui
// renderer (headless, and Cocoa until the Metal chrome lands) it
// degrades to the raw Ui.
Ui* createImguiUi(stl::ObjPool& owner, Composer& composer);
