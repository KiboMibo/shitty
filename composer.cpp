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
#include "font_path.h"
#include "glyph_cache.h"
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

u16 Composer::borderPixels() const {
    const float scaled = opts->border * contentScale;
    if (!(scaled > 0)) {
        return 0;
    }
    if (scaled >= 3000) {
        return 3000;
    }
    return (u16)(scaled + 0.5f);
}

void Composer::resize(u16 pixelWidth_, u16 pixelHeight_) {
    STD_ASSERT(glyphWidth != 0);
    STD_ASSERT(glyphHeight != 0);

    const u32 borders = 2u * borderPixels();
    const u32 contentWidth = pixelWidth_ > borders ? pixelWidth_ - borders : 0;
    const u32 contentHeight = pixelHeight_ > borders ? pixelHeight_ - borders : 0;
    const u16 columns_ = (u16)(max<u32>(1, contentWidth / glyphWidth));
    const u16 rows_ = (u16)(max<u32>(1, contentHeight / glyphHeight));

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return;
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
