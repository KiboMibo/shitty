/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render.h"

#include "render_metal.h"
#include "render_vk.h"

Renderer* Renderer::create(Composer& composer, const plt::RenderContext& context) {
    if (Renderer* const renderer = createMetalRenderer(composer, context)) {
        return renderer;
    }
    return createVulkanRenderer(composer, context);
}
