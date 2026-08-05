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
    struct FiberMutex;
    struct InputSink;
    struct Platform;
    struct Window;
}

struct Fontpack;
struct Application;
struct Brand;
struct CellExtraStore;
struct Font;
struct FontFace;
struct FontMetrics;
struct FontRenderer;
struct InputBindings;
struct InputRemap;
struct Options;
struct Renderer;
struct SessionSet;
struct Pty;
struct Vterm;
struct FontRequest;

enum class FontKind : u8;

// Application wiring. Components retain Composer itself and read dependencies
// here when needed; they do not cache aliases of these canonical fields.
// Event producers commit state here before notifying listeners.
struct Composer {
    explicit Composer(stl::ObjPool* pool);
    Composer(stl::ObjPool* pool, Brand& brand);

    void setContentScale(float scale);
    void setGlyphSize(u16 width, u16 height);
    void setCellExtras(CellExtraStore* extras);
    void resize(u16 pixelWidth, u16 pixelHeight);
    Font* loadFont(stl::ObjPool& owner, const FontRequest& request, FontMetrics& metrics);
    // Adopts a face fresh from a resolver and rasterizes it with the first
    // renderer in fontRenderers that succeeds; null when none does.
    Font* renderFace(stl::ObjPool& owner, FontFace* face, u16 pixels, FontKind kind, FontMetrics& metrics);

    stl::ObjPool* pool = nullptr;
    Brand* brand = nullptr;
    // Owns the renderer and its listeners; dropped and rebuilt wholesale
    // when the renderer loses its surface.
    stl::ObjPool::Ref rendererPool = stl::ObjPool::fromMemory();
    stl::SmallObjAllocator* smallObjects = nullptr;
    Application* application = nullptr;
    CellExtraStore* cellExtras = nullptr;
    Fontpack* fonts = nullptr;
    InputBindings* inputBindings = nullptr;
    // Chord rewriting; created after the options are parsed, so it stays
    // null with no remap configured and for early events.
    InputRemap* inputRemap = nullptr;
    // Runtime configuration. The constructor installs zeroed defaults for
    // headless adapters; Application replaces them with parsed options.
    Options* opts = nullptr;
    plt::InputSink* input = nullptr;
    stl::Output* ptyOutput = nullptr;
    Renderer* renderer = nullptr;
    Pty* pty = nullptr;
    SessionSet* sessions = nullptr;
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
    // Font renderers are tried in registration order; any renderer accepts
    // any resolver's face.
    stl::IntrusiveList fontRenderers;
    // Input producers call input; the router walks this list in registration
    // order and stops at the first handler which accepts the event.
    stl::IntrusiveList inputHandlers;
    // Terminal actions are claimed once for the window. Every terminal
    // pushes its own node here and unlinks it when it stops being the one
    // the window shows, so the action follows the active terminal without
    // the binding table ever being re-registered.
    stl::IntrusiveList copyListeners;
    stl::IntrusiveList pasteListeners;
    stl::IntrusiveList pastePrimaryListeners;
    stl::IntrusiveList pageUpListeners;
    stl::IntrusiveList pageDownListeners;
    stl::IntrusiveList newTabListeners;
    stl::IntrusiveList closeTabListeners;
    stl::IntrusiveList prevTabListeners;
    stl::IntrusiveList nextTabListeners;
};
