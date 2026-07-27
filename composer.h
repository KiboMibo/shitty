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
struct Font;
struct FontMetrics;
struct InputBindings;
struct InputSink;
struct Renderer;
struct Pty;
struct PtyEventHost;
struct PtyEventSource;
struct Vterm;
struct Window;
struct FontRequest;

// Application wiring. Components retain Composer itself and read dependencies
// here when needed; they do not cache aliases of these canonical fields.
// Event producers commit state here before notifying listeners.
struct Composer {
    explicit Composer(stl::ObjPool* pool);

    void setContentScale(float scale);
    void setGlyphSize(u16 width, u16 height);
    void setCellExtras(CellExtraStore* extras);
    void resize(u16 pixelWidth, u16 pixelHeight);
    Font* loadFont(stl::ObjPool& owner, const FontRequest& request, FontMetrics& metrics);

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
    float contentScale = 1.0f;

    // resize() commits all geometry fields before walking this list.
    stl::IntrusiveList resizedListeners;
    stl::IntrusiveList contentScaleChangedListeners;
    stl::IntrusiveList fontIncListeners;
    stl::IntrusiveList fontDecListeners;
    stl::IntrusiveList fontResetListeners;
    stl::IntrusiveList fontChangedListeners;
    stl::IntrusiveList cellExtrasChangedListeners;
    // Font resolvers are tried in registration order.
    stl::IntrusiveList fontResolvers;
    // Input producers call input; the router walks this list in registration
    // order and stops at the first sink which accepts the event.
    stl::IntrusiveList inputSinks;
};
