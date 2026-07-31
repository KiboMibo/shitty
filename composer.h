/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
    class Output;
    class SmallObjAllocator;
}

namespace plt {
    struct InputSink;
    struct Platform;
    struct Window;
}

struct Fontpack;
struct Application;
struct CellExtraStore;
struct Clipboard;
struct DesktopActions;
struct Font;
struct FontMetrics;
struct InputBindings;
struct Renderer;
struct Pty;
struct PtyOutputQueue;
struct Vterm;
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
    // Owns the renderer and its listeners; dropped and rebuilt wholesale
    // when the renderer loses its surface.
    stl::ObjPool::Ref rendererPool = stl::ObjPool::fromMemory();
    stl::SmallObjAllocator* smallObjects = nullptr;
    Application* application = nullptr;
    CellExtraStore* cellExtras = nullptr;
    Clipboard* clipboard = nullptr;
    DesktopActions* desktopActions = nullptr;
    Fontpack* fonts = nullptr;
    InputBindings* inputBindings = nullptr;
    plt::InputSink* input = nullptr;
    stl::Output* ptyOutput = nullptr;
    PtyOutputQueue* ptyOutputs = nullptr;
    Renderer* renderer = nullptr;
    Pty* pty = nullptr;
    plt::Platform* platform = nullptr;
    Vterm* vterm = nullptr;
    plt::Window* window = nullptr;

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
    // order and stops at the first handler which accepts the event.
    stl::IntrusiveList inputHandlers;
};
