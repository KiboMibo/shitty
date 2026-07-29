/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct Composer;
struct Renderer;
struct TerminalCell;

namespace plt {
    struct RenderContext;
}

Renderer* createVulkanRenderer(Composer& composer, const plt::RenderContext& context);
u32 vulkanRendererCellAttributesForTest(const TerminalCell& cell);
