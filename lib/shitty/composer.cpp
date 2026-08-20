/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "brand.h"
#include "cell_extra_store.h"
#include "font_coretext.h"
#include "font_embedded.h"
#include "font_face.h"
#include "font_fontconfig.h"
#include "font_freetype.h"
#include "font_pack.h"
#include "font_path.h"
#include "glyph_cache.h"
#include "grid_geometry.h"
#include "options.h"
#include "font_renderer.h"
#include "font_resolver.h"
#include "input_bindings.h"
#include "input_router.h"
#include "listener.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>
#include <std/mem/small_obj_allocator.h>
#include <std/sys/throw.h>

#include <stdio.h>

using namespace stl;

Composer::Composer(ObjPool* pool_)
    : Composer(pool_, *Brand::generic())
{
}

Composer::Composer(ObjPool* pool_, Brand& brand_)
    : pool(pool_)
    , brand(&brand_)
{
    opts = pool->make<Options>();
    cellExtras = CellExtraStore::create(*this, 0);
    smallObjects = SmallObjAllocator::create(pool);
    glyphs = createGlyphCache(*pool);
    input = createInputRouter(*this);
    inputBindings = InputBindings::create(*this);
    inputHandlers.pushBack(inputBindings);
    // The terminal actions belong to the window, not to a terminal: they
    // are claimed once here so any number of terminals can contribute a
    // listener to them, and so the lists outlive every terminal that does.
    inputBindings->add(InputActions::Copy, &copyListeners);
    inputBindings->add(InputActions::Paste, &pasteListeners);
    inputBindings->add(InputActions::PastePrimary, &pastePrimaryListeners);
    inputBindings->add(InputActions::PageUp, &pageUpListeners);
    inputBindings->add(InputActions::PageDown, &pageDownListeners);
    inputBindings->add(InputActions::NewTab, &newTabListeners);
    inputBindings->add(InputActions::CloseTab, &closeTabListeners);
    // Before PrevTab/NextTab: chords match in registration order, so the
    // -naturalEditing rows for cmd+arrows win over the tab walk exactly
    // while the preset holds.
    inputBindings->add(InputActions::LineStart, &lineStartListeners);
    inputBindings->add(InputActions::LineEnd, &lineEndListeners);
    inputBindings->add(InputActions::PrevTab, &prevTabListeners);
    inputBindings->add(InputActions::NextTab, &nextTabListeners);
    for (unsigned at = 0; at < 9; ++at) {
        inputBindings->add((InputActions)((unsigned)(InputActions::SelectTab1) + at), &selectTabListeners[at]);
    }
    inputBindings->add(InputActions::Clear, &clearListeners);
    inputBindings->add(InputActions::ToggleSidebar, &toggleSidebarListeners);
    inputBindings->add(InputActions::WordLeft, &wordLeftListeners);
    inputBindings->add(InputActions::WordRight, &wordRightListeners);
    inputBindings->add(InputActions::KillLine, &killLineListeners);
    inputBindings->add(InputActions::EraseWord, &eraseWordListeners);
    if (FontResolver* const resolver = createCoreTextFontResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontResolver* const resolver = createFontconfigResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontResolver* const resolver = createPathFontResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontResolver* const resolver = createEmbeddedFontResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontRenderer* const renderer = createCoreTextFontRenderer(*this)) {
        fontRenderers.pushBack(renderer);
    }
    if (FontRenderer* const renderer = createFreeTypeFontRenderer(*this)) {
        fontRenderers.pushBack(renderer);
    }
}

void Composer::setContentScale(float scale) {
    STD_ASSERT(scale > 0.0f);
    if (contentScale == scale) {
        return;
    }
    contentScale = scale;
    for (IntrusiveNode* node = contentScaleChangedListeners.mutFront(); node != contentScaleChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

void Composer::setGlyphSize(u16 width, u16 height) {
    STD_ASSERT(width != 0);
    STD_ASSERT(height != 0);
    if (glyphWidth == width && glyphHeight == height) {
        return;
    }
    glyphWidth = width;
    glyphHeight = height;
}

float Composer::boxDrawingStroke() const {
    if (fonts != nullptr) {
        const float measured = fonts->boxDrawingStroke();
        if (measured > 0.0f) {
            return measured;
        }
    }
    const u16 shortSide = glyphWidth < glyphHeight ? glyphWidth : glyphHeight;
    const float fallback = (float)(shortSide) / 12.0f;
    return fallback > 1.0f ? fallback : 1.0f;
}

void Composer::setCellExtras(CellExtraStore* extras) {
    if (cellExtras == extras) {
        return;
    }
    cellExtras = extras;
    for (IntrusiveNode* node = cellExtrasChangedListeners.mutFront(); node != cellExtrasChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

u16 Composer::scaledPixels(u16 points) const {
    const float scaled = points * contentScale;
    if (!(scaled > 0)) {
        return 0;
    }
    // Saturation only, and out of reach of every legal option value at
    // every scale a display has: both options that reach here are
    // validated in points and capped at 3000 of them, so this bites at
    // scale 10 and never before. A ceiling of 3000 *pixels* used to bite
    // at scale 2, where it froze the reserve while the chrome drawn from
    // the same option kept growing (R4-qa, Q1). Half of u16 is what
    // leaves room for the border this is summed with in contentInsets().
    if (scaled >= 30000) {
        return 30000;
    }
    return (u16)(scaled + 0.5f);
}

u16 Composer::borderPixels() const {
    return scaledPixels(opts->border);
}

Insets Composer::chromeInsets() const {
    return Insets{
        scaledPixels(chromeReserves[(unsigned)(ChromeSide::Top)]),
        scaledPixels(chromeReserves[(unsigned)(ChromeSide::Right)]),
        scaledPixels(chromeReserves[(unsigned)(ChromeSide::Bottom)]),
        scaledPixels(chromeReserves[(unsigned)(ChromeSide::Left)]),
    };
}

Insets Composer::paneInsets() const {
    // One call, four sides: the border is symmetric by definition, and
    // reading it once is also what keeps the four sides consistent if a
    // reload changes the option between two of them.
    const u16 border = borderPixels();
    return Insets{border, border, border, border};
}

Insets Composer::contentInsets() const {
    // A10: the sum, and nothing else - no third reading of either
    // option, so the composition cannot drift from the two parts it is
    // made of.
    const Insets chrome = chromeInsets();
    const Insets pane = paneInsets();
    return Insets{
        (u16)(pane.top + chrome.top),
        (u16)(pane.right + chrome.right),
        (u16)(pane.bottom + chrome.bottom),
        (u16)(pane.left + chrome.left),
    };
}

u16 Composer::chromeReserve(ChromeSide side) const {
    STD_ASSERT(side != ChromeSide::Count);
    return chromeReserves[(unsigned)(side)];
}

void Composer::setChromeReserve(ChromeSide side, u16 points) {
    STD_ASSERT(side != ChromeSide::Count);
    u16& stored = chromeReserves[(unsigned)(side)];
    if (stored == points) {
        return;
    }
    stored = points;
    // The content box just changed under a surface that did not, so the
    // grid has to be counted out of it again - this is the whole of what
    // makes cmd+b widen the terminal and send the shell its new size
    // (A7). resize() itself decides whether anything actually moved and
    // stays silent when it did not. Before the first font and the first
    // surface there is no grid to count: showWindow()'s own resize()
    // picks the reserve up when it runs.
    if (pixelWidth != 0 && pixelHeight != 0 && glyphWidth != 0 && glyphHeight != 0) {
        resize(pixelWidth, pixelHeight);
    }
}

void Composer::resize(u16 pixelWidth_, u16 pixelHeight_) {
    STD_ASSERT(glyphWidth != 0);
    STD_ASSERT(glyphHeight != 0);

    const Insets insets = contentInsets();
    const u16 columns_ = (u16)(gridColumns(pixelWidth_, insets, glyphWidth));
    const u16 rows_ = (u16)(gridRows(pixelHeight_, insets, glyphHeight));

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return;
    }

    // F4, Q2. Every re-count of the grid passes through here, which is
    // why the trace does too. It used to live in
    // ApplicationImpl::updateWindowInfo(), i.e. on the platform's own
    // callback, so a grid changed by setChromeReserve() - cmd+b, a
    // reload, the title bar strip appearing - printed nothing at all:
    // R4-qa moved the grid three times with cmd+b and read zero
    // `window:` lines, which made T6's "no resize events on hover"
    // criterion unable to fail. The full-screen transition bugs the
    // trace was written for are still visible in it: what a platform
    // delivers reaches this function unchanged.
    if (opts->verbose && (columns != columns_ || rows != rows_)) {
        fprintf(stderr, "%s: window: %ux%u px, grid %ux%u -> %ux%u, scale %.2f\n", brand->identifierCString(), (unsigned)(pixelWidth_), (unsigned)(pixelHeight_), (unsigned)(columns), (unsigned)(rows), (unsigned)(columns_), (unsigned)(rows_), (double)(contentScale));
    }

    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;

    for (IntrusiveNode* node = resizedListeners.mutFront(); node != resizedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

Font* Composer::loadFont(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    for (IntrusiveNode* node = fontResolvers.mutFront(); node != fontResolvers.mutEnd();) {
        FontResolver* const resolver = static_cast<FontResolver*>(node);
        node = node->next;
        FontFace* const resolved = resolver->resolve(request);
        if (resolved != nullptr) {
            return renderFace(owner, resolved, request.pixels, request.kind, metrics);
        }
    }
    return nullptr;
}

Font* Composer::renderFace(ObjPool& owner, FontFace* face, u16 pixels, FontKind kind, FontMetrics& metrics) {
    const IntrusivePtr<FontFace> adopted(face);
    for (IntrusiveNode* node = fontRenderers.mutFront(); node != fontRenderers.mutEnd();) {
        FontRenderer* const renderer = static_cast<FontRenderer*>(node);
        node = node->next;
        try {
            Font* const font = renderer->render(owner, adopted, pixels, kind, metrics);
            if (font != nullptr) {
                return font;
            }
        } catch (Exception&) {
            // A renderer that cannot open or fit the face passes it on.
        }
    }
    return nullptr;
}
