/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "brand.h"
#include "options.h"
#include "font_face.h"
#include "font_pack.h"
#include "font_path.h"
#include "glyph_cache.h"
#include "input_router.h"
#include "font_coretext.h"
#include "font_embedded.h"
#include "font_freetype.h"
#include "font_renderer.h"
#include "font_resolver.h"
#include "grid_geometry.h"
#include "input_bindings.h"
#include "font_fontconfig.h"
#include "cell_extra_store.h"

#include <lib/vterm/listener.h>

#include <std/sys/throw.h>
#include <std/alg/minmax.h>
#include <std/dbg/assert.h>
#include <std/mem/small_obj_allocator.h>

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
    vt.pool = pool;
    setOptions(pool->make<Options>());
    vt.brandName = brand->displayName();
    vt.cellExtras = CellExtraStore::create(vt, 0);
    vt.smallObjects = SmallObjAllocator::create(pool);
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
    // The splits go in on 'd', which no other row claims on either
    // modifier set, so their position here settles nothing - but the
    // position is still theirs on purpose: find() answers with the first
    // match, and a later row for cmd+d would be unreachable rather than
    // ambiguous.
    inputBindings->add(InputActions::SplitVertical, &splitVerticalListeners);
    inputBindings->add(InputActions::SplitHorizontal, &splitHorizontalListeners);
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
    if (vt.contentScale == scale) {
        return;
    }
    vt.contentScale = scale;
    for (IntrusiveNode* node = contentScaleChangedListeners.mutFront(); node != contentScaleChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

void Composer::setOptions(const Options* options) {
    opts = options;
    vt.config = &options->vt;
    vt.baseBorder = options->border;
}

float Composer::boxDrawingStroke() const {
    if (fonts != nullptr) {
        const float measured = fonts->boxDrawingStroke();
        if (measured > 0.0f) {
            return measured;
        }
    }
    const u16 shortSide = vt.glyphWidth < vt.glyphHeight ? vt.glyphWidth : vt.glyphHeight;
    const float fallback = (float)(shortSide) / 12.0f;
    return fallback > 1.0f ? fallback : 1.0f;
}

u16 Composer::scaledPixels(u16 points) const {
    const float scaled = points * vt.contentScale;
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
    // vt.baseBorder and not opts->border: setOptions() is the one place
    // a snapshot is published, and reading the field it publishes is
    // what keeps the two from being two.
    return scaledPixels(vt.baseBorder);
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
    if (vt.pixelWidth != 0 && vt.pixelHeight != 0 && vt.glyphWidth != 0 && vt.glyphHeight != 0) {
        resize(vt.pixelWidth, vt.pixelHeight);
    }
}

void Composer::resize(u16 pixelWidth_, u16 pixelHeight_) {
    STD_ASSERT(vt.glyphWidth != 0);
    STD_ASSERT(vt.glyphHeight != 0);

    const Insets insets = contentInsets();
    const u16 columns_ = (u16)(gridColumns(pixelWidth_, insets, vt.glyphWidth));
    const u16 rows_ = (u16)(gridRows(pixelHeight_, insets, vt.glyphHeight));

    if (vt.columns == columns_ && vt.rows == rows_ && vt.pixelWidth == pixelWidth_ && vt.pixelHeight == pixelHeight_) {
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
    if (vt.config->verbose && (vt.columns != columns_ || vt.rows != rows_)) {
        fprintf(stderr, "%s: window: %ux%u px, grid %ux%u -> %ux%u, scale %.2f\n", brand->identifierCString(), (unsigned)(pixelWidth_), (unsigned)(pixelHeight_), (unsigned)(vt.columns), (unsigned)(vt.rows), (unsigned)(columns_), (unsigned)(rows_), (double)(vt.contentScale));
    }

    vt.columns = columns_;
    vt.rows = rows_;
    vt.pixelWidth = pixelWidth_;
    vt.pixelHeight = pixelHeight_;

    for (IntrusiveNode* node = vt.resizedListeners.mutFront(); node != vt.resizedListeners.mutEnd();) {
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
