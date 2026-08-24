/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <lib/vterm/vt_state.h>

#include <std/lib/list.h>
#include <std/sys/types.h>
#include <std/mem/obj_pool.h>

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
struct Config;
struct CellExtraStore;
struct Font;
struct FontFace;
struct FontMetrics;
struct FontRenderer;
struct GlyphCache;
struct InputBindings;
struct InputRemap;
struct Options;
struct Renderer;
struct SpanShaper;
struct SessionSet;
struct Pty;
struct LaunchCommand;
struct Vterm;
struct VtermTraceFactory;
struct FontRequest;

enum class FontKind : u8;

// Application wiring. Components retain Composer itself and read dependencies
// here when needed; they do not cache aliases of these canonical fields.
// Event producers commit state here before notifying listeners.
struct Composer {
    explicit Composer(stl::ObjPool* pool);
    Composer(stl::ObjPool* pool, Brand& brand);

    void setContentScale(float scale);
    // Publishes a parsed snapshot: the core's view (vt.config, the base
    // border) follows the swap atomically.
    void setOptions(const Options* options);
    float boxDrawingStroke() const;
    Font* loadFont(stl::ObjPool& owner, const FontRequest& request, FontMetrics& metrics);
    // Adopts a face fresh from a resolver and rasterizes it with the first
    // renderer in fontRenderers that succeeds; null when none does.
    Font* renderFace(stl::ObjPool& owner, FontFace* face, u16 pixels, FontKind kind, FontMetrics& metrics);

    VtState vt;
    stl::ObjPool* pool = nullptr;
    Brand* brand = nullptr;
    // Owns the renderer and its listeners; dropped and rebuilt wholesale
    // when the renderer loses its surface.
    stl::ObjPool::Ref rendererPool = stl::ObjPool::fromMemory();
    Application* application = nullptr;
    Fontpack* fonts = nullptr;
    // The rasterized-glyph memo shared by every font backend; fonts key
    // it with private namespaces, so it never needs resetting on a font
    // change - stale strikes age out through the byte budget.
    GlyphCache* glyphs = nullptr;
    InputBindings* inputBindings = nullptr;
    // Chord rewriting; created after the options are parsed, so it stays
    // null for early events.
    InputRemap* inputRemap = nullptr;
    // Immutable runtime configuration. The constructor installs zeroed
    // defaults for headless adapters; Config publishes parsed snapshots.
    const Options* opts = nullptr;
    Config* config = nullptr;
    plt::InputSink* input = nullptr;
    Renderer* renderer = nullptr;
    // The render side of the cell grid: shapes rows into strips through
    // fonts. The model never touches it; created with the fontpack
    // machinery and null in purely headless embeddings.
    SpanShaper* shaper = nullptr;
    // Process-lifetime PTY factory and the immutable command each new
    // session launches. Individual handles never leave SessionSet.
    Pty* pty = nullptr;
    const LaunchCommand* launch = nullptr;
    VtermTraceFactory* vtermTraceFactory = nullptr;
    SessionSet* sessions = nullptr;

    u16 fontSize = 0;

    stl::IntrusiveList contentScaleChangedListeners;
    stl::IntrusiveList fontIncListeners;
    stl::IntrusiveList fontDecListeners;
    stl::IntrusiveList fontResetListeners;
    // SessionSet commits its tab model - count, order, active index,
    // labels - and then walks this list; the window chrome projects the
    // model from here.
    stl::IntrusiveList sessionsChangedListeners;
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
    // One list per direct-selection chord; index N serves SelectTab1+N.
    stl::IntrusiveList selectTabListeners[9];
    stl::IntrusiveList clearListeners;
    stl::IntrusiveList wordLeftListeners;
    stl::IntrusiveList wordRightListeners;
    stl::IntrusiveList lineStartListeners;
    stl::IntrusiveList lineEndListeners;
    stl::IntrusiveList killLineListeners;
    stl::IntrusiveList eraseWordListeners;
};
