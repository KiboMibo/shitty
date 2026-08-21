/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_metal.h"

#include "render_push_constants.h"

#include "brand.h"
#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "listener.h"
#include "options.h"
#include "render.h"
#include "render_arena.h"
#include "render_msl.h"
#include "screen.h"
#include "vterm.h"

#include <plt/window.h>

#include <std/ios/sys.h>
#include <std/alg/minmax.h>
#include <std/sys/atomic.h>
#include <std/dbg/assert.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sys/crt.h>

#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#undef Rect
#undef Point

#include <sched.h>
#include <stdio.h>

using namespace stl;

namespace {
    static constexpr u32 gpuBold = 1u << 2;
    static constexpr u32 gpuItalic = 1u << 3;
    static constexpr u32 gpuUnderline = 1u << 4;
    static constexpr u32 gpuInverse = 1u << 5;
    static constexpr u32 gpuWrap = 1u << 6;
    static constexpr u32 gpuFaint = 1u << 8;
    static constexpr u32 gpuBlink = 1u << 9;
    static constexpr u32 gpuConceal = 1u << 10;
    static constexpr u32 gpuStrike = 1u << 11;
    static constexpr u32 gpuOverline = 1u << 12;
    static constexpr u32 gpuUnderlineStyle = 0x7u << 13;
    static constexpr u32 gpuDoubleWidth = 1u << 16;
    static constexpr u32 gpuDoubleWidthContinuation = 1u << 17;
    static constexpr u32 gpuProtection = 0x3u << 18;
    static constexpr u32 gpuDrawn = 1u << 20;
    static constexpr u32 framesInFlight = 3;

    // strip: the pixel offset of the cell's slice base in its plane's
    // arena, the top bit selecting the color plane; stripNone marks a
    // cell with no strip (blank, or coverage the shader synthesizes).
    static constexpr u32 stripNone = 0xffffffffu;
    static constexpr u32 stripColorPlane = 0x80000000u;

    struct GpuCell {
        u32 codepoint = ' ';
        u32 attributes = 0;
        u32 foreground = 0;
        u32 background = 0;
        u32 underlineColor = 0;
        u32 hyperlink = 0;
        u32 strip = stripNone;
        u32 stripStride = 0;
        u32 semantic = 0;
        u32 lineAttribute = 0;
    };

    static_assert(sizeof(GpuCell) == 40, "Metal cell layout mismatch");

    struct GpuCellUpdate {
        u32 sourceIndex;
        u32 outputIndex;
        GpuCell cell;
    };

    static_assert(sizeof(GpuCellUpdate) == 48, "Metal cell update layout mismatch");

    // R9-1: the block lives in render_push_constants.h now - one
    // definition for both backends, so the two cannot drift apart.
    using PushConstants = GpuPushConstants;


    struct PresentationState {
        TerminalCursor cursor;
        // F9: the pane's live default background (OSC 11), captured per
        // pane because the fill pass paints this pane's rectangle with
        // it - and two panes of one window disagree about it as freely
        // as two windows do.
        Color background;
        Rect selection;
        Color selectionForeground;
        Color selectionBackground;
        u8 selectionColorMask = 0;
        u32 hoveredHyperlink = 0;
        u32 hoveredLinkBegin = 0;
        u32 hoveredLinkEnd = 0;
        bool screenReverse = false;
        bool blinkVisible = true;
        bool cursorBlink = false;
    };

    // A2: one pane's slice of the frame - where it goes on the
    // drawable, where its cells live in the shared vector, and the state
    // it was captured with. Every pane's cells sit in one vector because
    // one frame is one command buffer: every pane's dispatch is encoded
    // before the drawable presents, so every pane's cells exist at once.
    struct PaneRender {
        PixelRect area;
        size_t cellOffset = 0;
        Screen* shapes = nullptr;
        // A9: the pane's own grid - the shape of the cells at
        // cellOffset, and what the shader is handed to decode its own
        // indices with. Not the window's: two panes of one window have
        // two grids the moment the window has a split.
        u16 columns = 0;
        u16 rows = 0;
        PresentationState state;
        // Where this pane's cell updates land in the frame's buffer,
        // filled by buildCellUpdates() and read by the dispatch.
        u32 updateOffset = 0;
        u32 updateCount = 0;
    };

    struct PresentationFrame {
        id<MTLCommandBuffer> commandBuffer = nil;
        // Per-frame: draw() waits only this frame's fence, so a shared
        // update buffer would be rewritten while older frames still read
        // it on the GPU.
        id<MTLBuffer> cellBuffer = nil;
        size_t cellCapacity = 0;
    };

    struct MetalRendererImpl;

    struct CallMetalFontChanged final: public Listener {
        explicit CallMetalFontChanged(MetalRendererImpl* renderer);

        void onListen(void*) override;

        MetalRendererImpl* renderer;
    };

    struct MetalRendererImpl final: public Renderer {
        MetalRendererImpl(Composer& composer, CAMetalLayer* layer);
        ~MetalRendererImpl();

        bool initialize();
        bool update(const PaneUpdate* frame, size_t count) override;
        bool update(const TerminalUpdate& update) override;
        void setSeams(const PixelRect* seams, size_t count, Color ink) override;
        bool updateOnce(const PaneUpdate* frame, size_t count);
        bool repaint() override;

        bool headless() const;
        void resetFontResources();
        void materializeCells(const TerminalCell* input, GpuCell* output, u16 count, u8 lineAttribute, const TerminalColors& colors);
        bool ensureArenaBuffer(id<MTLBuffer>& buffer, size_t& capacity, size_t needed, bool& replaced);
        bool uploadArenas();
        void biasStrips(const PaneRender& pane, size_t maskBase, size_t colorBase);
        void applySpanStrips(size_t cellBase, u16 columns, const ScreenRowSpan& span);
        void assignRowStrips(Screen& shapes, u16 columns, u16 row, size_t cellOffset);
        void overrideOverlayStrips(Screen& shapes, u16 columns, const TerminalUpdate& update, size_t cellOffset);
        u32 assignStrips(const TerminalUpdate& update, size_t cellOffset);
        bool ensureTargets(u32 width, u32 height);
        bool ensureCellBuffer(PresentationFrame& frame, size_t count);
        u32 buildCellUpdates(PresentationFrame& frame);
        u32 buildPaneUpdates(GpuCellUpdate* updates, const PaneRender& pane, u32 updateCount);
        bool draw();
        bool captureOutput(stl::Buffer& rgb, u32& width, u32& height) override;
        void waitFrames();
        void destroyTargets();
        void destroyFontResources();
        static void capture(PresentationState& state, const TerminalUpdate& update);
        static u32 packColor(Color color);

        Composer& composer;
        CAMetalLayer* metalLayer;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLComputePipelineState> pipeline = nil;
        // The strip arenas mirrored on the device; append-only between
        // collections, so only the tail copies each frame.
        id<MTLBuffer> maskArena = nil;
        id<MTLBuffer> colorArena = nil;
        // The frame target when there is no layer to present to: the
        // shadow renderer of the test harness draws into a texture and
        // captureOutput() reads it back. Nil in the interactive path,
        // where the target is the drawable and belongs to the layer.
        id<MTLTexture> offscreen = nil;
        size_t maskArenaCapacity = 0;
        size_t colorArenaCapacity = 0;
        // A3: what the device mirrors, one range per pane. A pane's
        // generation is only meaningful next to the pane that produced
        // it, and both planes lay their panes out in the same order, so
        // the copies and the panes share their indices.
        PaneArenaMirror maskMirror;
        PaneArenaMirror colorMirror;
        Vector<PaneArenaRequest> maskRequests;
        Vector<PaneArenaRequest> colorRequests;
        Vector<PaneArenaCopy> maskCopies;
        Vector<PaneArenaCopy> colorCopies;
        Color clearBackground = composer.opts->bg;
        // F9: the seams of the frame being drawn, and their colour.
        Vector<PixelRect> seams;
        Color seamInk;
        PresentationFrame frames[framesInFlight];
        u32 currentFrame = 0;
        u32 outputWidth = 0;
        u32 outputHeight = 0;
        // The retained cells of every pane, one grid after another.
        Vector<GpuCell> cells;
        Vector<PaneRender> panes;
        Vector<ScreenRowSpan> spanScratch;
        // Presents in flight through the async path; draw() skips
        // instead of blocking in nextDrawable when the drawables are
        // busy, and teardown drains this before releasing anything a
        // completion handler touches.
        u32 inflightPresents = 0;
        bool transactionalPresent = true;
        bool ready = false;
    };

}

CallMetalFontChanged::CallMetalFontChanged(MetalRendererImpl* renderer_)
    : renderer(renderer_)
{
}

void CallMetalFontChanged::onListen(void*) {
    renderer->resetFontResources();
}

MetalRendererImpl::MetalRendererImpl(Composer& composer_, CAMetalLayer* layer_)
    : composer(composer_)
    , metalLayer([layer_ retain])
{
}

// No layer, no drawables, no present: the shadow renderer of the test
// harness. Everything above the target is the interactive path unchanged
// - the same pipeline, the same push constants, the same dispatches -
// which is the whole point of comparing its pixels against the reference
// renderer's.
bool MetalRendererImpl::headless() const {
    return metalLayer == nil;
}

MetalRendererImpl::~MetalRendererImpl() {
    waitFrames();
    destroyTargets();
    [offscreen release];
    offscreen = nil;
    destroyFontResources();
    for (PresentationFrame& frame : frames) {
        [frame.cellBuffer release];
        frame.cellBuffer = nil;
        frame.cellCapacity = 0;
    }
    [pipeline release];
    [queue release];
    [device release];
    [metalLayer release];
}

bool MetalRendererImpl::initialize() {
    device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        return false;
    }
    queue = [device newCommandQueue];
    if (queue == nil) {
        return false;
    }
    NSString* const source = [[NSString alloc] initWithBytes:renderMetalSource length:sizeof(renderMetalSource) - 1 encoding:NSUTF8StringEncoding];
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (library == nil) {
        if (error != nil) {
            sysE << StringView(u8"Metal shader compilation failed: ") << StringView(error.localizedDescription.UTF8String) << endL;
        }
        return false;
    }
    id<MTLFunction> function = [library newFunctionWithName:@"main0"];
    if (function != nil) {
        pipeline = [device newComputePipelineStateWithFunction:function error:&error];
    }
    [function release];
    [library release];
    if (pipeline == nil) {
        if (error != nil) {
            sysE << StringView(u8"Metal pipeline creation failed: ") << StringView(error.localizedDescription.UTF8String) << endL;
        }
        return false;
    }

    // The shader dereferences both arenas unconditionally; give them
    // valid storage before the first strips arrive.
    bool replaced = false;
    if (!ensureArenaBuffer(maskArena, maskArenaCapacity, 4, replaced) || !ensureArenaBuffer(colorArena, colorArenaCapacity, 4, replaced)) {
        return false;
    }

    if (headless()) {
        ready = true;
        return ready;
    }

    // The compute shader writes cell pixels straight into the drawable, so the
    // layer must not be framebuffer-only. presentsWithTransaction starts on:
    // draw() keeps it on only during live resize, where a drawable must
    // present inside the same CoreAnimation transaction as the bounds change
    // (no resize flicker), and turns it off everywhere else for asynchronous
    // presents. allowsNextDrawableTimeout=NO makes nextDrawable block for a
    // free drawable instead of returning nil; the in-flight gate in draw()
    // keeps that wait short.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = NO;
    metalLayer.presentsWithTransaction = YES;
    metalLayer.maximumDrawableCount = framesInFlight;
    metalLayer.allowsNextDrawableTimeout = NO;
    metalLayer.contentsGravity = kCAGravityTopLeft;
    metalLayer.magnificationFilter = kCAFilterNearest;
    metalLayer.minificationFilter = kCAFilterNearest;
    // The shader writes sRGB-encoded bytes into an _Unorm drawable; tag the
    // layer sRGB so the compositor reads them as sRGB (matches the previous
    // IOSurface colour-space tagging).
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (colorSpace != nullptr) {
        metalLayer.colorspace = colorSpace;
        CGColorSpaceRelease(colorSpace);
    }
    [CATransaction commit];

    ready = true;
    return ready;
}

void MetalRendererImpl::destroyFontResources() {
    [maskArena release];
    [colorArena release];
    maskArena = nil;
    colorArena = nil;
    maskArenaCapacity = 0;
    colorArenaCapacity = 0;
    maskMirror.reset();
    colorMirror.reset();
}

void MetalRendererImpl::resetFontResources() {
    waitFrames();
    // The screens reset their arenas with the font: the device mirrors
    // nothing any of them holds now.
    maskMirror.reset();
    colorMirror.reset();
    cells.clear();
    panes.clear();
}

// `replaced` says the storage is new and holds nothing, so whatever
// the mirror thinks the device has is no longer true.
bool MetalRendererImpl::ensureArenaBuffer(id<MTLBuffer>& buffer, size_t& capacity, size_t needed, bool& replaced) {
    replaced = false;
    if (needed < 4) {
        needed = 4;
    }
    if (buffer != nil && capacity >= needed) {
        return true;
    }
    size_t next = capacity < 4096 ? 4096 : capacity;
    while (next < needed) {
        next *= 2;
    }
    // Growth is rare (font or viewport change); a drain keeps the swap
    // trivially safe against frames in flight.
    waitFrames();
    [buffer release];
    buffer = [device newBufferWithLength:next options:MTLResourceStorageModeShared];
    replaced = true;
    if (buffer == nil) {
        capacity = 0;
        return false;
    }
    capacity = next;
    return true;
}

// A3. The panes' arenas lie back to back on the device in draw order;
// each pane owes the tail it has not sent, or its whole range when its
// own strips moved or the panes before it grew and pushed it along. One
// pane plans base zero and its tail, which is what the single-generation
// code this replaces did, byte for byte and copy for copy.
bool MetalRendererImpl::uploadArenas() {
    const size_t count = panes.length();
    maskCopies.clear();
    maskCopies.zero(count);
    colorCopies.clear();
    colorCopies.zero(count);
    maskMirror.plan(maskRequests.data(), count, maskCopies.mutData());
    colorMirror.plan(colorRequests.data(), count, colorCopies.mutData());

    // A6-5: both mirrors, on either failure. The two plans are made
    // before either allocation and the copies happen after both, so a
    // failure between them leaves the *other* mirror holding a plan
    // whose copies were never made - device bytes it claims are there
    // and are not. render_arena.h:64 states the contract in exactly
    // these words: a caller that cannot make the copies calls reset().
    // The next frame would otherwise draw glyphs out of another pane's
    // bytes and say nothing.
    bool replaced = false;
    if (!ensureArenaBuffer(maskArena, maskArenaCapacity, maskMirror.used(), replaced)) {
        maskMirror.reset();
        colorMirror.reset();
        return false;
    }
    if (replaced) {
        // New storage holds nothing: the plan that assumed the old one
        // is void, and everything is owed again.
        maskMirror.reset();
        maskMirror.plan(maskRequests.data(), count, maskCopies.mutData());
    }
    if (!ensureArenaBuffer(colorArena, colorArenaCapacity, colorMirror.used() * sizeof(u32), replaced)) {
        maskMirror.reset();
        colorMirror.reset();
        return false;
    }
    if (replaced) {
        colorMirror.reset();
        colorMirror.plan(colorRequests.data(), count, colorCopies.mutData());
    }

    // A copy that starts at a pane's own zero rewrites bytes an in-flight
    // frame may still be reading; a tail lands beyond all of them.
    bool restarts = false;
    for (size_t index = 0; index < count; ++index) {
        restarts = restarts || (maskCopies[index].from == 0 && maskCopies[index].to != 0);
        restarts = restarts || (colorCopies[index].from == 0 && colorCopies[index].to != 0);
    }
    if (restarts) {
        waitFrames();
    }

    for (size_t index = 0; index < count; ++index) {
        const PaneRender& pane = panes[index];
        if (pane.shapes == nullptr) {
            continue;
        }
        const PaneArenaCopy& mask = maskCopies[index];
        if (mask.to > mask.from) {
            memcpy((u8*)(maskArena.contents) + mask.base + mask.from, pane.shapes->spanMask() + mask.from, mask.to - mask.from);
        }
        // The color plane counts u32 pixels, matching the offsets the
        // strips carry; the buffer counts bytes.
        const PaneArenaCopy& color = colorCopies[index];
        if (color.to > color.from) {
            memcpy((u8*)(colorArena.contents) + (color.base + color.from) * sizeof(u32), (const u8*)(pane.shapes->spanColor() + color.from), (color.to - color.from) * sizeof(u32));
        }
        biasStrips(pane, mask.base, color.base);
    }
    return true;
}

// The strips a pane hands out are offsets into its own arena; on the
// device that arena starts at its base. The first pane's base is zero,
// and on a one-pane frame that is every pane there is - so this walk
// costs nothing until a window has two of them.
void MetalRendererImpl::biasStrips(const PaneRender& pane, size_t maskBase, size_t colorBase) {
    if (maskBase == 0 && colorBase == 0) {
        return;
    }
    // A9: this pane's cells, counted by this pane's grid. The window's
    // would walk past the end of the last pane and, when the panes
    // differ in size, bias the neighbour's strips as well.
    const size_t count = (size_t)(pane.columns) * pane.rows;
    for (size_t index = 0; index < count; ++index) {
        GpuCell& cell = cells.mut(pane.cellOffset + index);
        if (cell.strip == stripNone) {
            continue;
        }
        cell.strip += (u32)((cell.strip & stripColorPlane) != 0 ? colorBase : maskBase);
    }
}

void MetalRendererImpl::applySpanStrips(size_t cellBase, u16 columns, const ScreenRowSpan& span) {
    if (span.missing || span.end <= span.begin || span.end > columns) {
        return;
    }
    const u32 stride = (u32)(span.end - span.begin) * composer.glyphWidth;
    for (u16 column = span.begin; column < span.end; ++column) {
        GpuCell& cell = cells.mut(cellBase + column);
        cell.strip = (span.offset + (u32)(column - span.begin) * composer.glyphWidth) | (span.color ? stripColorPlane : 0);
        cell.stripStride = stride;
    }
}

void MetalRendererImpl::assignRowStrips(Screen& shapes, u16 columns, u16 row, size_t cellOffset) {
    const size_t rowIndex = cellOffset + (size_t)(row)*columns;
    GpuCell* const rowCells = cells.mutData() + rowIndex;
    for (u16 column = 0; column < columns; ++column) {
        rowCells[column].strip = stripNone;
        rowCells[column].stripStride = 0;
    }
    const size_t count = shapes.rowSpans(row, spanScratch.mutData());
    for (size_t index = 0; index < count; ++index) {
        applySpanStrips(rowIndex, columns, spanScratch[index]);
    }
}

void MetalRendererImpl::overrideOverlayStrips(Screen& shapes, u16 columns, const TerminalUpdate& update, size_t cellOffset) {
    if (update.overlayCount == 0) {
        return;
    }
    // The preedit preview covers the underlying strips wholesale: its
    // blank cells hide the text below them.
    const size_t rowIndex = cellOffset + (size_t)(update.overlayRow) * columns;
    for (u32 index = 0; index < update.overlayCount; ++index) {
        GpuCell& cell = cells.mut(rowIndex + update.overlayColumn + index);
        cell.strip = stripNone;
        cell.stripStride = 0;
    }
    const size_t count = shapes.shapeCells(update.overlayCells, update.overlayCount, update.overlayColumn, spanScratch.mutData());
    for (size_t index = 0; index < count; ++index) {
        applySpanStrips(rowIndex, columns, spanScratch[index]);
    }
}

u32 MetalRendererImpl::assignStrips(const TerminalUpdate& update, size_t cellOffset) {
    Screen& shapes = *update.shapes;
    // A9: the grid of the pane this update belongs to.
    const u16 columns = update.gridColumns;
    spanScratch.clear();
    spanScratch.grow(columns);
    while (spanScratch.length() < columns) {
        spanScratch.pushBack({});
    }
    // A shaping pass can collect the arenas and move every strip assigned
    // so far, so redo the walk until it closes within one generation.
    u32 generation;
    do {
        generation = shapes.spanGeneration();
        for (u16 row = 0; row < update.gridRows; ++row) {
            assignRowStrips(shapes, columns, row, cellOffset);
        }
        overrideOverlayStrips(shapes, columns, update, cellOffset);
    } while (generation != shapes.spanGeneration());
    return generation;
}

void MetalRendererImpl::materializeCells(const TerminalCell* input, GpuCell* outputCells, u16 count, u8 lineAttribute, const TerminalColors& colors) {
    CellExtraStore& extras = *composer.cellExtras;
    const bool specialColors = colors.specialModes != 0;
    for (u16 index = 0; index < count; ++index) {
        const TerminalCell& cell = input[index];
        const u32 codepoint = cell.uc_pt ? cell.uc_pt : ' ';
        const u32 attributes = cellAttributes(cell);
        const u32 foreground = specialColors ? colors.resolveForegroundSpecial(cell).packed() : colors.resolvePacked(cell.foreground());
        const u32 background = specialColors ? colors.resolveBackgroundSpecial(cell).packed() : colors.resolvePacked(cell.background());
        u32 underlineColor = foreground;
        u32 hyperlink = 0;
        if (cell.hasExtra()) {
            const CellExtraView extra = extras.view(cell);
            hyperlink = extra.hyperlinkDisplayId;
            if (cell.underlined() && extra.underlineColor != cell.foreground()) {
                underlineColor = colors.resolvePacked(extra.underlineColor);
            }
        } else if (cell.underlined() && cell.inlineUnderlineColor() != cell.foreground()) {
            underlineColor = colors.resolvePacked(cell.inlineUnderlineColor());
        }

        // The strip reference arrives in the row pass that follows the
        // span materialization; a cell it skips has none.
        outputCells[index] = {
            codepoint,
            attributes,
            foreground,
            background,
            underlineColor,
            hyperlink,
            stripNone,
            0,
            cell.semantic,
            lineAttribute,
        };
    }
}

bool MetalRendererImpl::ensureCellBuffer(PresentationFrame& frame, size_t count) {
    if (frame.cellCapacity >= count) {
        return true;
    }
    [frame.cellBuffer release];
    frame.cellBuffer = [device newBufferWithLength:count * sizeof(GpuCellUpdate) options:MTLResourceStorageModeShared];
    if (frame.cellBuffer == nil) {
        frame.cellCapacity = 0;
        return false;
    }
    frame.cellCapacity = count;
    return true;
}

void MetalRendererImpl::waitFrames() {
    for (PresentationFrame& frame : frames) {
        if (frame.commandBuffer != nil) {
            [frame.commandBuffer waitUntilCompleted];
            [frame.commandBuffer release];
            frame.commandBuffer = nil;
        }
    }
    // Completion handlers run concurrently with waitUntilCompleted
    // returning; drain them before anyone frees what they touch.
    while (stdAtomicFetch(&inflightPresents, __ATOMIC_ACQUIRE) != 0) {
        sched_yield();
    }
}

void MetalRendererImpl::destroyTargets() {
    waitFrames();
    outputWidth = 0;
    outputHeight = 0;
}

bool MetalRendererImpl::ensureTargets(u32 width, u32 height) {
    if (outputWidth == width && outputHeight == height) {
        return true;
    }
    // Drain in-flight frames before changing the drawable size so no queued
    // command buffer still targets a drawable of the old size.
    waitFrames();
    if (headless()) {
        [offscreen release];
        // Private storage, because a render pass clears this texture and
        // a render target must be private on the Macs that do not share
        // memory with the GPU; captureOutput() blits it into a shared
        // buffer, exactly as the Vulkan backend copies its image.
        MTLTextureDescriptor* const descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        descriptor.storageMode = MTLStorageModePrivate;
        offscreen = [device newTextureWithDescriptor:descriptor];
        if (offscreen == nil) {
            outputWidth = 0;
            outputHeight = 0;
            return false;
        }
    } else {
        metalLayer.drawableSize = CGSizeMake(width, height);
    }
    outputWidth = width;
    outputHeight = height;
    currentFrame = 0;
    return true;
}

// A9: one pane's cells, walked by that pane's own grid. Split out of
// buildCellUpdates() only so the column count is a parameter of the walk
// rather than a member the whole frame shares - two panes of one window
// no longer have one grid between them.
u32 MetalRendererImpl::buildPaneUpdates(GpuCellUpdate* updates, const PaneRender& pane, u32 updateCount) {
    const u16 columns = pane.columns;
    const u32 paneCells = (u32)(columns)*pane.rows;
    for (u32 sourceIndex = 0; sourceIndex < paneCells; ++sourceIndex) {
        const u32 sourceRow = sourceIndex / columns;
        const u32 sourceColumn = sourceIndex - sourceRow * columns;
        const u32 rowIndex = sourceRow * columns;
        const size_t cellIndex = pane.cellOffset + sourceIndex;
        const u8 lineAttribute = (u8)(cells[pane.cellOffset + rowIndex].lineAttribute);
        if (lineAttribute == 0) {
            GpuCell cell = cells[cellIndex];
            if ((cell.attributes & gpuDoubleWidth) != 0 && (sourceColumn + 1 >= columns || (cells[cellIndex + 1].attributes & gpuDoubleWidthContinuation) == 0)) {
                cell.attributes &= ~gpuDoubleWidth;
            }
            updates[updateCount++] = {sourceIndex, sourceIndex, cell};
            continue;
        }
        const u32 outputColumn = sourceColumn * 2;
        if (outputColumn >= columns) {
            continue;
        }
        updates[updateCount++] = {sourceIndex, rowIndex + outputColumn, cells[cellIndex]};
        if (outputColumn + 1 < columns) {
            updates[updateCount++] = {sourceIndex, rowIndex + outputColumn + 1, cells[cellIndex]};
        }
    }
    return updateCount;
}

u32 MetalRendererImpl::buildCellUpdates(PresentationFrame& frame) {
    if (!ensureCellBuffer(frame, cells.length())) {
        return 0;
    }
    auto* const updates = (GpuCellUpdate*)(frame.cellBuffer.contents);
    u32 updateCount = 0;
    // The indices the shader reads are the pane's own: it turns them
    // into a row and a column with the pane's column count and places
    // them at the pane's origin. Only the cell they are read from knows
    // which pane's grid it belongs to.
    for (size_t index = 0; index < panes.length(); ++index) {
        PaneRender& pane = panes.mut(index);
        pane.updateOffset = updateCount;
        updateCount = buildPaneUpdates(updates, pane, updateCount);
        pane.updateCount = updateCount - pane.updateOffset;
    }
    return updateCount;
}

u32 MetalRendererImpl::packColor(Color color) {
    return (u32)(color.red) | ((u32)(color.green) << 8) | ((u32)(color.blue) << 16);
}

void MetalRendererImpl::setSeams(const PixelRect* bands, size_t count, Color ink) {
    seams.clear();
    for (size_t at = 0; at < count; ++at) {
        seams.pushBack(bands[at]);
    }
    seamInk = ink;
}

void MetalRendererImpl::capture(PresentationState& state, const TerminalUpdate& update) {
    state.cursor = update.cursor;
    state.background = update.colors != nullptr ? update.colors->defaultBackground : Color{};
    state.selection = update.snappedSelection;
    state.selectionForeground = update.selectionForeground;
    state.selectionBackground = update.selectionBackground;
    state.selectionColorMask = update.selectionColorMask;
    state.hoveredHyperlink = update.hoveredHyperlink;
    state.hoveredLinkBegin = update.hoveredLinkBegin;
    state.hoveredLinkEnd = update.hoveredLinkEnd;
    state.screenReverse = update.screenReverse;
    state.blinkVisible = update.blinkVisible;
    state.cursorBlink = update.cursorBlink;
}

bool MetalRendererImpl::draw() {
    if (!ready || panes.empty() || cells.empty() || outputWidth == 0 || outputHeight == 0) {
        return false;
    }
    // Transaction-synchronous presents are for live resize only, where
    // bounds and contents must commit together. Everywhere else the
    // present is asynchronous: a flooding child must not serialize the
    // parser behind vsync (the wall-time gap of the cat benchmark).
    // Neither applies without a layer: there is nothing to present to,
    // and the frame is waited for so that captureOutput() reads it.
    const bool transactional = !headless() && composer.window != nullptr && composer.window->inLiveResize();
    if (!headless() && transactional != transactionalPresent) {
        if (transactional) {
            // Entering a resize: let the async presents land first so
            // the transactional frame is the newest.
            while (stdAtomicFetch(&inflightPresents, __ATOMIC_ACQUIRE) != 0) {
                sched_yield();
            }
        }
        metalLayer.presentsWithTransaction = transactional ? YES : NO;
        transactionalPresent = transactional;
    }
    if (!headless() && !transactional && stdAtomicFetch(&inflightPresents, __ATOMIC_ACQUIRE) >= 2) {
        // Drawables are busy: skip without blocking. The caller retries
        // on the next frame callback and the retained cells redraw
        // everything then.
        return false;
    }
    PresentationFrame& frame = frames[currentFrame];
    if (frame.commandBuffer != nil) {
        [frame.commandBuffer waitUntilCompleted];
        [frame.commandBuffer release];
        frame.commandBuffer = nil;
    }

    const u32 updateCount = buildCellUpdates(frame);
    if (updateCount == 0) {
        return false;
    }
    id<CAMetalDrawable> drawable = headless() ? nil : [metalLayer nextDrawable];
    if (drawable == nil && !headless()) {
        return false;
    }
    id<MTLTexture> target = headless() ? offscreen : drawable.texture;
    if (target == nil) {
        return false;
    }
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (commandBuffer == nil) {
        return false;
    }
    [commandBuffer retain];
    frame.commandBuffer = commandBuffer;

    MTLRenderPassDescriptor* clearPass = [MTLRenderPassDescriptor renderPassDescriptor];
    clearPass.colorAttachments[0].texture = target;
    clearPass.colorAttachments[0].loadAction = MTLLoadActionClear;
    clearPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    clearPass.colorAttachments[0].clearColor = MTLClearColorMake(clearBackground.red / 255.0, clearBackground.green / 255.0, clearBackground.blue / 255.0, 1.0);
    id<MTLRenderCommandEncoder> clear = [commandBuffer renderCommandEncoderWithDescriptor:clearPass];
    [clear endEncoding];

    // A10: the pane rectangle plus the border. The chrome reserves came
    // off the window before the rectangles were cut, so adding them here
    // would charge every pane for a sidebar it is not next to.
    const Insets insets = composer.paneInsets();
    // The spirv-cross assignment for this shader: push constants at
    // buffer 0, the color arena at 1, the mask arena at 2, the cell
    // updates at 3, the drawable at texture 0.
    id<MTLComputeCommandEncoder> compute = [commandBuffer computeCommandEncoder];
    [compute setComputePipelineState:pipeline];
    [compute setBuffer:colorArena offset:0 atIndex:1];
    [compute setBuffer:maskArena offset:0 atIndex:2];
    [compute setTexture:target atIndex:0];
    // A2: one dispatch per pane, all of them in this one frame's
    // encoder. Each pane's constants place its grid at its own rectangle
    // and hand the shader that rectangle's far edge as the output bounds
    // it already clips every pixel against - which is how a pane that
    // draws wider than its cells (a double-width line in the last
    // column) stops at its neighbour instead of inside it.
    for (size_t index = 0; index < panes.length(); ++index) {
        const PaneRender& pane = panes[index];
        if (pane.updateCount == 0) {
            continue;
        }
        const PresentationState& state = pane.state;
        const PushConstants constants{
            composer.glyphWidth,
            composer.glyphHeight,
            composer.boxDrawingStroke(),
            pane.columns,
            pane.rows,
            min<u32>(outputWidth, (u32)(pane.area.x) + pane.area.width),
            min<u32>(outputHeight, (u32)(pane.area.y) + pane.area.height),
            (u32)(pane.area.x) + insets.left,
            (u32)(pane.area.y) + insets.top,
            packColor(state.cursor.color),
            state.cursor.posX,
            state.cursor.posY,
            (u32)(state.cursor.style),
            state.screenReverse ? 1u : 0u,
            state.selection.tl.x,
            state.selection.tl.y,
            state.selection.br.x,
            state.selection.br.y,
            state.selection.rectangular ? 1u : 0u,
            composer.opts->showWraps ? 1u : 0u,
            packColor(state.selectionForeground),
            packColor(state.selectionBackground),
            state.selectionColorMask,
            state.blinkVisible ? 1u : 0u,
            state.cursorBlink ? 1u : 0u,
            state.hoveredHyperlink,
            state.hoveredLinkBegin,
            state.hoveredLinkEnd,
            pane.updateCount,
            pane.area.x,
            pane.area.y,
            packColor(pane.state.background),
        };
        [compute setBuffer:frame.cellBuffer offset:(size_t)(pane.updateOffset) * sizeof(GpuCellUpdate) atIndex:3];
        // F9: render.h's contract - "the backend clears that rectangle" -
        // in two passes. The fill paints the pane's own rectangle with its
        // own background, the cells then draw over it, and what neither
        // touches is the seam, which keeps the colour the drawable was
        // cleared with. Before this the padding of every pane but the
        // first wore the first pane's background, which is a defect on
        // its own: two panes with different OSC 11 already drew wrong.
        //
        // Two dispatches rather than one because a single dispatch has no
        // order between its invocations: a cell written before the fill
        // reached it would be painted out. The encoder is the default
        // serial one, so the ordering between the two is the encoder's to
        // keep and needs no explicit barrier.
        PushConstants fill = constants;
        fill.paneBackgroundAndFill |= fillPassBit;
        const u32 paneWidth = constants.outputWidth > fill.paneLeft ? constants.outputWidth - fill.paneLeft : 0;
        const u32 paneHeight = constants.outputHeight > fill.paneTop ? constants.outputHeight - fill.paneTop : 0;
        if (paneWidth != 0 && paneHeight != 0) {
            [compute setBytes:&fill length:sizeof(fill) atIndex:0];
            [compute dispatchThreads:MTLSizeMake((size_t)(paneWidth) * paneHeight, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        }
        [compute setBytes:&constants length:sizeof(constants) atIndex:0];
        [compute dispatchThreads:MTLSizeMake(pane.updateCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    }
    // F9: the seams last, over both neighbours. Each band lies in the air
    // the two panes leave between their grids, and each of them has just
    // filled that air with its own background - so a seam painted earlier
    // would be half wiped by whichever pane drew second.
    //
    // Painted with the fill pass the panes already use: the same kernel,
    // handed the band as its rectangle and the seam's colour as its
    // background. No second pipeline, and nothing here that has to know
    // how panes are laid out.
    for (const PixelRect& seam : seams) {
        PushConstants band{};
        band.glyphWidth = composer.glyphWidth;
        band.glyphHeight = composer.glyphHeight;
        band.outputWidth = min<u32>(outputWidth, (u32)(seam.x) + seam.width);
        band.outputHeight = min<u32>(outputHeight, (u32)(seam.y) + seam.height);
        band.paneLeft = seam.x;
        band.paneTop = seam.y;
        band.paneBackgroundAndFill = packColor(seamInk) | fillPassBit;
        const u32 bandWidth = band.outputWidth > band.paneLeft ? band.outputWidth - band.paneLeft : 0;
        const u32 bandHeight = band.outputHeight > band.paneTop ? band.outputHeight - band.paneTop : 0;
        if (bandWidth == 0 || bandHeight == 0) {
            continue;
        }
        [compute setBytes:&band length:sizeof(band) atIndex:0];
        [compute dispatchThreads:MTLSizeMake((size_t)(bandWidth) * bandHeight, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    }
    [compute endEncoding];

    if (headless()) {
        // Nothing to present, and the caller may read the texture the
        // moment this returns: the harness asks for the pixels in the
        // same breath as the frame.
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
    } else if (transactional) {
        // Present synchronously into the current CoreAnimation
        // transaction, so bounds and contents commit together. draw()
        // only ever runs on the main thread, which the layout engine
        // requires for an in-transaction present.
        [commandBuffer commit];
        [commandBuffer waitUntilScheduled];
        [drawable present];
    } else {
        stdAtomicAddAndFetch(&inflightPresents, 1, __ATOMIC_ACQ_REL);
        MetalRendererImpl* const renderer = this;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            stdAtomicSubAndFetch(&renderer->inflightPresents, 1, __ATOMIC_ACQ_REL);
        }];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
    currentFrame = (currentFrame + 1) % framesInFlight;
    return true;
}

// The frame just drawn, as tightly packed RGB rows - the parity oracle
// this backend had none of. Only the shadow renderer answers: the
// interactive path hands its drawable to the compositor and keeps
// nothing, and blitting every presented frame into a readable buffer to
// keep it would cost the frame the README's numbers are measured on. The
// Vulkan backend draws the same line, at chain->readback.
bool MetalRendererImpl::captureOutput(Buffer& rgb, u32& width, u32& height) {
    if (offscreen == nil || outputWidth == 0 || outputHeight == 0) {
        return false;
    }
    waitFrames();
    width = outputWidth;
    height = outputHeight;
    const size_t rowBytes = (size_t)(width) * 4;
    const size_t bytes = rowBytes * height;
    id<MTLBuffer> staging = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (staging == nil) {
        return false;
    }
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (commandBuffer == nil) {
        [staging release];
        return false;
    }
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:offscreen sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1) toBuffer:staging destinationOffset:0 destinationBytesPerRow:rowBytes destinationBytesPerImage:bytes];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    // The texture holds sRGB-encoded bytes (the shader encodes them) in
    // BGRA order, so the swizzle is the whole conversion - the same one
    // the Vulkan capture makes for its B8G8R8A8 formats.
    const auto* const source = (const u8*)(staging.contents);
    rgb.reset();
    for (size_t pixel = 0; pixel < (size_t)(width)*height; ++pixel) {
        const u8 values[3] = {source[4 * pixel + 2], source[4 * pixel + 1], source[4 * pixel]};
        rgb.append(values, 3);
    }
    [staging release];
    return true;
}

bool MetalRendererImpl::repaint() {
    return draw();
}

bool MetalRendererImpl::update(const PaneUpdate* frame, size_t count) {
    for (;;) {
        try {
            return updateOnce(frame, count);
        } catch (const FontFaceMiss& miss) {
            // Lost-surface style: adopt a face for the missed cluster (or
            // record that nothing serves it) and re-run the frame - the
            // whole of it, panes already materialized included, so no
            // pane is left shaped through the fontpack that lost.
            composer.fonts->adoptFaceFor(miss);
        }
    }
}

bool MetalRendererImpl::update(const TerminalUpdate& update) {
    const PaneUpdate pane = surfacePane(composer, update);
    return this->update(&pane, 1);
}

bool MetalRendererImpl::updateOnce(const PaneUpdate* frame, size_t count) {
    if (!ready || count == 0) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        if (frame[index].update.colors == nullptr) {
            return false;
        }
    }
    const u32 width = composer.pixelWidth;
    const u32 height = composer.pixelHeight;
    if (width == 0 || height == 0 || !ensureTargets(width, height)) {
        return false;
    }

    // A2: the panes' grids lie one after another in `cells`, so a frame
    // that gained or lost a pane reshapes the vector exactly as a
    // resized grid does - and retains nothing, because nothing in it is
    // where it was.
    //
    // A6-4: which pane, and not just how many. The retain is matched by
    // the Screen each pane shapes through - the identity the arena
    // mirror already keys on (render_arena.h). A frame with as many
    // panes as the last one but in another order (a neighbour closed and
    // the tree rebalanced, two panes swapped on a drag) would otherwise
    // leave every retained grid where it was and let pane 0 draw pane
    // 1's undamaged rows. That is the defect A3 closed one structure
    // higher, in the arenas; it lived on here, in the cells.
    size_t cellCount = 0;
    bool shapeChanged = panes.length() != count;
    for (size_t pane = 0; pane < count; ++pane) {
        const TerminalUpdate& update = frame[pane].update;
        // A9: zero is a refused frame, not a window-sized default - the
        // grid is the shape of the cells this update carries, and
        // reading them without it is a read of an unknown length.
        if (update.gridColumns == 0 || update.gridRows == 0) {
            return false;
        }
        if (!shapeChanged) {
            const PaneRender& previous = panes[pane];
            shapeChanged = previous.shapes != update.shapes || previous.columns != update.gridColumns || previous.rows != update.gridRows || previous.cellOffset != cellCount;
        }
        cellCount += (size_t)(update.gridColumns) * update.gridRows;
    }
    if (cellCount == 0) {
        return false;
    }
    shapeChanged = shapeChanged || cells.length() != cellCount;
    if (shapeChanged) {
        // A reshaped grid needs every row before the retained cells mean
        // anything.
        for (size_t pane = 0; pane < count; ++pane) {
            const TerminalUpdate& update = frame[pane].update;
            if (update.rowCount != update.gridRows) {
                return false;
            }
            for (size_t index = 0; index < update.rowCount; ++index) {
                if (update.rows[index].cells == nullptr || update.rows[index].row != index) {
                    return false;
                }
            }
        }
        cells.clear();
        cells.zero(cellCount);
    }

    panes.clear();
    maskRequests.clear();
    colorRequests.clear();
    size_t cellOffset = 0;
    for (size_t pane = 0; pane < count; ++pane) {
        const TerminalUpdate& update = frame[pane].update;
        const u16 paneColumns = update.gridColumns;
        const u16 paneRows = update.gridRows;
        for (size_t index = 0; index < update.rowCount; ++index) {
            const TerminalRow& row = update.rows[index];
            if (row.cells == nullptr || row.row >= paneRows) {
                return false;
            }
            materializeCells(row.cells, cells.mutData() + cellOffset + (size_t)(row.row) * paneColumns, paneColumns, row.lineAttribute, *update.colors);
        }
        if (update.overlayCount != 0 && update.overlayCells != nullptr && update.overlayRow < paneRows && (size_t)(update.overlayColumn) + update.overlayCount <= paneColumns) {
            // The preview covers the row content beneath it.
            materializeCells(update.overlayCells, cells.mutData() + cellOffset + (size_t)(update.overlayRow) * paneColumns + update.overlayColumn, update.overlayCount, 0, *update.colors);
        }
        panes.pushBack(PaneRender{frame[pane].area, cellOffset, update.shapes, paneColumns, paneRows, {}, 0, 0});
        capture(panes.mutBack().state, update);
        // A3: the generation travels with the pane that produced it, and
        // a pane without a screen still takes its place in the list so
        // that the panes, the requests and the copies share one index.
        const u32 generation = update.shapes != nullptr ? assignStrips(update, cellOffset) : 0;
        maskRequests.pushBack({update.shapes, generation, update.shapes != nullptr ? update.shapes->spanMaskUsed() : 0});
        colorRequests.pushBack({update.shapes, generation, update.shapes != nullptr ? update.shapes->spanColorUsed() : 0});
        cellOffset += (size_t)(paneColumns)*paneRows;
    }
    // The two walks above count the same cells: the first to size the
    // vector, the second to place each pane in it. They are two loops
    // because the shape has to be settled before a single cell is
    // written, and this is what says they still agree - a disagreement
    // is a pane materializing past the end of the vector, which writes
    // and reads back consistently and so shows up in no pixel at all.
    STD_ASSERT(cellOffset == cellCount);
    if (!uploadArenas()) {
        return false;
    }
    // What the panes do not cover is the window's own air - the chrome
    // reserve, before the chrome has drawn over it. Whose background
    // that is when the panes disagree used to be an open question here,
    // deferred to T9; F9 answers the half that mattered by making each
    // pane paint its own rectangle (the fill pass in draw()), so this
    // colour is now only ever seen outside every pane.
    clearBackground = frame[0].update.colors->defaultBackground;
    return draw();
}

Renderer* createMetalRenderer(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context) {
    // Headless is the shadow renderer of the test harness: no layer, a
    // texture for a target, and captureOutput() to read it. The Vulkan
    // backend takes the same context and builds a headless surface for
    // the same reason.
    const bool headless = context.backend == plt::RenderBackend::Headless;
    if (!headless && (context.backend != plt::RenderBackend::Cocoa || context.connection == nullptr)) {
        return nullptr;
    }
    auto* const renderer = pool.make<MetalRendererImpl>(composer, headless ? nil : (CAMetalLayer*)(context.connection));
    if (!renderer->initialize()) {
        return nullptr;
    }
    composer.fontChangedListeners.pushBack(pool.make<CallMetalFontChanged>(renderer));
    return renderer;
}
