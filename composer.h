/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/list.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Fontpack;
struct Application;
struct CellExtraStore;
struct Clipboard;
struct DesktopActions;
struct InputBindings;
struct InputSink;
struct Renderer;
struct Pty;
struct PtyEventHost;
struct PtyEventSource;
struct Vterm;
struct Window;

// Application wiring. Components copy the dependencies they need during
// creation. Event producers publish canonical state here and listeners read
// it after notification, without knowing one another.
struct Composer {
    explicit Composer(stl::ObjPool* pool);

    void setGlyphSize(u16 width, u16 height);
    void resize(u16 pixelWidth, u16 pixelHeight);

    stl::ObjPool* pool = nullptr;
    Application* application = nullptr;
    CellExtraStore* cellExtras = nullptr;
    Clipboard* clipboard = nullptr;
    DesktopActions* desktopActions = nullptr;
    Fontpack* fonts = nullptr;
    InputBindings* inputBindings = nullptr;
    InputSink* input = nullptr;
    Renderer* renderer = nullptr;
    Pty* pty = nullptr;
    PtyEventHost* ptyEventHost = nullptr;
    PtyEventSource* ptyEvents = nullptr;
    Vterm* vterm = nullptr;
    Window* window = nullptr;

    u16 columns = 0;
    u16 rows = 0;
    u16 pixelWidth = 0;
    u16 pixelHeight = 0;
    u16 glyphWidth = 0;
    u16 glyphHeight = 0;
    u16 fontSize = 0;

    // resize() commits all geometry fields before walking this list.
    stl::IntrusiveList resizedListeners;
    stl::IntrusiveList fontIncListeners;
    stl::IntrusiveList fontDecListeners;
    stl::IntrusiveList fontResetListeners;
    stl::IntrusiveList fontChangedListeners;
    // Input producers call input; the router walks this list in registration
    // order and stops at the first sink which accepts the event.
    stl::IntrusiveList inputSinks;
};
