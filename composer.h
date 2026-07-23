/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class ObjPool;
}

struct Fontpack;
struct Application;
struct CellExtraStore;
struct Renderer;
struct Pty;
struct PtyEventSource;
struct Vterm;

// Application wiring. Components copy the dependencies they need during
// creation; the composer only establishes the graph and its shared lifetime.
struct Composer {
    stl::ObjPool* pool = nullptr;
    Application* application = nullptr;
    CellExtraStore* cellExtras = nullptr;
    Fontpack* fonts = nullptr;
    Renderer* renderer = nullptr;
    Pty* pty = nullptr;
    PtyEventSource* ptyEvents = nullptr;
    Vterm* vterm = nullptr;
};
