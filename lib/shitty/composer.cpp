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
#include "input_bindings.h"
#include "font_fontconfig.h"

#include <lib/vterm/listener.h>
#include <lib/vterm/cell_extra_store.h>

#include <std/sys/throw.h>
#include <std/alg/minmax.h>
#include <std/dbg/assert.h>
#include <std/mem/small_obj_allocator.h>

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
