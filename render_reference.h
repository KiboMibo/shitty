/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "render.h"

#include <std/lib/buffer.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Composer;
struct TestApi;
struct TerminalUpdate;

namespace plt {
    struct RenderContext;
}

struct ReferenceImage {
    const u8* pixels = nullptr;
    size_t length = 0;
    u16 width = 0;
    u16 height = 0;
};

struct ReferenceRenderer: Renderer {
    virtual void attach(TestApi& testApi) = 0;
    virtual ReferenceImage image() const = 0;
    virtual TerminalUpdate renderUpdate() const = 0;

    virtual void snapshot(stl::Buffer& out) const = 0;
    virtual void modelSnapshot(stl::Buffer& out) const = 0;
    virtual void modelDigest(stl::Buffer& out) const = 0;
    virtual void renderState(stl::Buffer& out) const = 0;
    virtual void selectionState(stl::Buffer& out) const = 0;
    virtual void scrollbackState(stl::Buffer& out) const = 0;
    virtual void screenText(stl::Buffer& out) const = 0;
    virtual void lastUpdate(stl::Buffer& out) const = 0;
    virtual void lastUpdateRows(stl::Buffer& out) const = 0;
    virtual void resetUpdateStats() = 0;

    virtual u16 columns() const = 0;
    virtual u16 rows() const = 0;
    virtual u32 historyRows() const = 0;
    virtual u32 hoveredHyperlink() const = 0;
    virtual u32 hoveredLinkBegin() const = 0;
    virtual u32 hoveredLinkEnd() const = 0;

    static ReferenceRenderer* create(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context);
};
