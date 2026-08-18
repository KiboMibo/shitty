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
struct SessionSet;
struct Pty;
struct LaunchCommand;
struct TerminalUpdate;
struct Vterm;
struct VtermTraceFactory;
struct FontRequest;

enum class FontKind : u8;

// A1: the layout-facing counterpart to the user's `border` option. `border`
// keeps meaning "air around the text"; Insets is what layout actually
// consumes - border plus whatever chrome (sidebar, titlebar strip) reserves
// on that side. Composer::contentInsets() is the only supported source of
// layout geometry; borderPixels() stays for reading the option itself.
//
// Units: every field is in backing (physical) pixels, the same unit as
// pixelWidth/pixelHeight and PixelRect below - NOT points, and NOT the
// logical points some Options fields (sidebarWidth, quickCornerRadius) are
// documented in. Any points-denominated option value MUST be multiplied by
// contentScale before it lands in an Insets field, exactly as borderPixels()
// already scales opts->border. Skip that conversion and every reserve comes
// out half of what it should be on a 2x (Retina) display, which both
// misplaces the layout and misses the hit-test by the same factor.
struct Insets {
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;
    u16 left = 0;
};

// A2: one pane's placement on the render target, in surface pixels. Deliberately
// distinct from the cell-grid `Rect` (rect.h) used for text selection - that
// type is Point-pairs in row/column space and means something else entirely.
struct PixelRect {
    u16 x = 0;
    u16 y = 0;
    u16 width = 0;
    u16 height = 0;
};

// A2: one pane's contribution to a single rendered frame. Renderer::update()
// takes a contiguous run of these instead of a beginFrame()/updatePane()/
// endFrame() state machine - one call, one frame, one present, and there is
// no way to leave the sequence unbalanced.
//
// Fixed here as the contract; T8 is the one who actually adds
//   virtual bool update(const PaneUpdate* panes, size_t count) = 0;
// to Renderer (render.h) and implements it in every backend. The plan and
// architecture docs write this as `stl::Span<const PaneUpdate>` - this
// codebase's stl has no Span type, and the established idiom for a
// read-only contiguous view is a pointer plus a count (see
// TomlSink::tomlKey, Darts::create), so the contract uses that instead.
// The existing `update(const TerminalUpdate&)` becomes a thin wrapper over
// a single-element array; nothing that calls it today changes.
struct PaneUpdate {
    PixelRect area;
    const TerminalUpdate& update;
};

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
    u16 borderPixels() const;

    // A1: border (symmetric) plus chrome reserves (per side), in backing
    // pixels (see the unit note on Insets above). Chrome reserves are
    // always zero until T5 (sidebar) and T6 (autoHideChrome) wire theirs
    // in, so this returns a uniform border on all four sides today -
    // behavior is unchanged.
    Insets contentInsets() const {
        const u16 border = borderPixels();
        return Insets{border, border, border, border};
    }
    float boxDrawingStroke() const;
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
    // Process-lifetime PTY factory and the immutable command each new
    // session launches. Individual handles never leave SessionSet.
    Pty* pty = nullptr;
    const LaunchCommand* launch = nullptr;
    VtermTraceFactory* vtermTraceFactory = nullptr;
    SessionSet* sessions = nullptr;
    plt::Platform* platform = nullptr;
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
    stl::IntrusiveList configChangedListeners;
    // Vterms publish their own undecorated title here. The session owner
    // decides whether the source is visible and how the window presents it.
    stl::IntrusiveList titleChangedListeners;
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
