/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <plt/window.h>

namespace stl {
    class ObjPool;
}

struct Composer;

// The window chrome around the terminal widget: tabs and whatever else
// a personality draws besides the grid. Implementations differ per
// brand and platform - the raw Ui is today's bare surface, the cocoa Ui
// projects the session set into the native title bar, the imgui Ui owns
// the window as a docking-style ImGui window with the terminal in a
// composited layer. The rest of the system never learns which one runs:
// a Ui reads the composer, subscribes to its listener lists, calls its
// canonical fields, and hands the terminal renderer the surface to draw
// into. Knowledge points one way only.
struct Ui {
    // The surface the terminal renderer is created against and presents
    // into. A layered Ui hands out its terminal layer; losing it drops
    // the renderer through the usual lost-surface path and frame()
    // rebuilds against a fresh context.
    virtual plt::RenderContext terminalContext() = 0;
    // Bracket one window frame around the terminal's own present.
    // Window geometry only ever arrives with the frame, so the opening
    // half owns the window-to-grid translation: lay the chrome out,
    // size the terminal surface, and commit the grid geometry through
    // Composer::resize(). Everything else a chrome needs to hear
    // arrives through the composer's listener lists.
    virtual void beginFrame(const plt::WindowInfo& info) = 0;
    virtual void endFrame() = 0;
};

// The bare terminal: the widget is the whole window and there is no
// chrome. Every personality uses it wherever it has no native chrome.
Ui* createRawUi(stl::ObjPool& owner, Composer& composer);
