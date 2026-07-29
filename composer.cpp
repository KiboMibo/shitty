/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "font_coretext.h"
#include "font_fontconfig.h"
#include "font_path.h"
#include "options.h"
#include "font_resolver.h"
#include "input_bindings.h"
#include "input_router.h"
#include "listener.h"
#include "small_obj_allocator.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>

using namespace stl;

Composer::Composer(ObjPool* pool_)
    : pool(pool_)
{
    smallObjects = SmallObjAllocator::create(pool);
    input = createInputRouter(*this);
    inputBindings = InputBindings::create(*this);
    inputHandlers.pushBack(inputBindings);
    if (FontResolver* const resolver = createCoreTextFontResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontResolver* const resolver = createFontconfigResolver(*this)) {
        fontResolvers.pushBack(resolver);
    }
    if (FontResolver* const resolver = createPathFontResolver(*this)) {
        fontResolvers.pushBack(resolver);
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

void Composer::resize(u16 pixelWidth_, u16 pixelHeight_) {
    STD_ASSERT(glyphWidth != 0);
    STD_ASSERT(glyphHeight != 0);

    const u32 border = 2u * opts.border;
    const u32 contentWidth = pixelWidth_ > border ? pixelWidth_ - border : 0;
    const u32 contentHeight = pixelHeight_ > border ? pixelHeight_ - border : 0;
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
        Font* const font = resolver->load(owner, request, metrics);
        if (font != nullptr) {
            return font;
        }
    }
    return nullptr;
}
