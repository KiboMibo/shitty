/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_metal.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "listener.h"
#include "options.h"
#include "render.h"
#include "render_msl.h"
#include "screen.h"
#include "vterm.h"

#include <plt/window.h>

#include <std/alg/minmax.h>
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

#include <stdio.h>

using namespace stl;

namespace {
    constexpr u32 gpuBold = 1u << 2;
    constexpr u32 gpuItalic = 1u << 3;
    constexpr u32 gpuUnderline = 1u << 4;
    constexpr u32 gpuInverse = 1u << 5;
    constexpr u32 gpuWrap = 1u << 6;
    constexpr u32 gpuFaint = 1u << 8;
    constexpr u32 gpuBlink = 1u << 9;
    constexpr u32 gpuConceal = 1u << 10;
    constexpr u32 gpuStrike = 1u << 11;
    constexpr u32 gpuOverline = 1u << 12;
    constexpr u32 gpuUnderlineStyle = 0x7u << 13;
    constexpr u32 gpuDoubleWidth = 1u << 16;
    constexpr u32 gpuDoubleWidthContinuation = 1u << 17;
    constexpr u32 gpuProtection = 0x3u << 18;
    constexpr u32 gpuDrawn = 1u << 20;
    constexpr u32 framesInFlight = 3;

    // strip: the pixel offset of the cell's slice base in its plane's
    // arena, the top bit selecting the color plane; stripNone marks a
    // cell with no strip (blank, or coverage the shader synthesizes).
    constexpr u32 stripNone = 0xffffffffu;
    constexpr u32 stripColorPlane = 0x80000000u;

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

    struct PushConstants {
        u32 glyphWidth;
        u32 glyphHeight;
        u32 columns;
        u32 rows;
        u32 outputWidth;
        u32 outputHeight;
        u32 border;
        u32 cursorColor;
        i32 cursorX;
        i32 cursorY;
        u32 cursorStyle;
        u32 screenReverseVideo;
        i32 selectionLeft;
        i32 selectionTop;
        i32 selectionRight;
        i32 selectionBottom;
        u32 rectangularSelection;
        u32 showWraps;
        u32 selectionForeground;
        u32 selectionBackground;
        u32 selectionColorMask;
        u32 blinkVisible;
        u32 cursorBlink;
        u32 hoveredHyperlink;
        u32 hoveredLinkBegin;
        u32 hoveredLinkEnd;
        u32 updateCount;
    };

    static_assert(sizeof(PushConstants) == 108, "Metal push constant layout mismatch");

    struct PresentationState {
        TerminalCursor cursor;
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
        bool update(const TerminalUpdate& update) override;
        bool repaint() override;

        void resetFontResources();
        void materializeCells(const TerminalCell* input, GpuCell* output, u16 count, u8 lineAttribute, const TerminalColors& colors);
        bool ensureArenaBuffer(id<MTLBuffer>& buffer, size_t& capacity, size_t& uploaded, size_t needed);
        bool uploadArenas(Screen& shapes, u32 generation);
        void applySpanStrips(u32 rowIndex, const ScreenRowSpan& span);
        void assignRowStrips(Screen& shapes, u16 row);
        void overrideOverlayStrips(Screen& shapes, const TerminalUpdate& update);
        u32 assignStrips(const TerminalUpdate& update);
        bool ensureTargets(u32 width, u32 height);
        bool ensureCellBuffer(PresentationFrame& frame, size_t count);
        u32 buildCellUpdates(PresentationFrame& frame);
        bool draw();
        void waitFrames();
        void destroyTargets();
        void destroyFontResources();
        void capture(const TerminalUpdate& update);
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
        size_t maskArenaCapacity = 0;
        size_t colorArenaCapacity = 0;
        size_t maskArenaUploaded = 0;
        size_t colorArenaUploaded = 0;
        // The arena generation the device copies mirror; a mismatch means
        // the strips moved wholesale and everything re-uploads.
        u32 stripGeneration = 0;
        Color clearBackground = opts.bg;
        PresentationFrame frames[framesInFlight];
        u32 currentFrame = 0;
        u32 outputWidth = 0;
        u32 outputHeight = 0;
        Vector<GpuCell> cells;
        Vector<ScreenRowSpan> spanScratch;
        u16 cellColumns = 0;
        u16 cellRows = 0;
        PresentationState state;
        bool stateValid = false;
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

MetalRendererImpl::~MetalRendererImpl() {
    waitFrames();
    destroyTargets();
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
    textureStorageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;

    NSString* const source = [[NSString alloc] initWithBytes:renderMetalSource length:sizeof(renderMetalSource) - 1 encoding:NSUTF8StringEncoding];
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (library == nil) {
        if (error != nil) {
            fprintf(stderr, "Metal shader compilation failed: %s\n", error.localizedDescription.UTF8String);
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
            fprintf(stderr, "Metal pipeline creation failed: %s\n", error.localizedDescription.UTF8String);
        }
        return false;
    }

    // The shader dereferences both arenas unconditionally; give them
    // valid storage before the first strips arrive.
    if (!ensureArenaBuffer(maskArena, maskArenaCapacity, maskArenaUploaded, 4) || !ensureArenaBuffer(colorArena, colorArenaCapacity, colorArenaUploaded, 4)) {
        return false;
    }

    // The compute shader writes cell pixels straight into the drawable, so the
    // layer must not be framebuffer-only. presentsWithTransaction makes each
    // drawable present as part of the current CoreAnimation transaction: when
    // the frame is rendered from displayLayer: during a live resize, that is the
    // same transaction as the bounds change, so bounds and contents commit
    // together (no resize flicker). allowsNextDrawableTimeout=NO makes
    // nextDrawable block for a free drawable instead of returning nil.
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
    maskArenaUploaded = 0;
    colorArenaUploaded = 0;
}

void MetalRendererImpl::resetFontResources() {
    waitFrames();
    // The screen reset its arenas with the font; generation zero never
    // occurs there, so everything re-uploads on the next frame.
    maskArenaUploaded = 0;
    colorArenaUploaded = 0;
    stripGeneration = 0;
    cells.clear();
    cellColumns = 0;
    cellRows = 0;
    stateValid = false;
}

bool MetalRendererImpl::ensureArenaBuffer(id<MTLBuffer>& buffer, size_t& capacity, size_t& uploaded, size_t needed) {
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
    if (buffer == nil) {
        capacity = 0;
        uploaded = 0;
        return false;
    }
    capacity = next;
    uploaded = 0;
    return true;
}

bool MetalRendererImpl::uploadArenas(Screen& shapes, u32 generation) {
    const size_t maskUsed = shapes.spanMaskUsed();
    const size_t colorUsed = shapes.spanColorUsed() * sizeof(u32);
    if (generation != stripGeneration) {
        // The strips moved wholesale (collection or font change): the
        // device copies restart from the beginning, which rewrites bytes
        // an in-flight frame may still read.
        waitFrames();
        maskArenaUploaded = 0;
        colorArenaUploaded = 0;
    }
    if (!ensureArenaBuffer(maskArena, maskArenaCapacity, maskArenaUploaded, maskUsed) || !ensureArenaBuffer(colorArena, colorArenaCapacity, colorArenaUploaded, colorUsed)) {
        return false;
    }
    if (maskUsed > maskArenaUploaded) {
        // The tail lands beyond anything an in-flight frame reads.
        memcpy((u8*)(maskArena.contents) + maskArenaUploaded, shapes.spanMask() + maskArenaUploaded, maskUsed - maskArenaUploaded);
        maskArenaUploaded = maskUsed;
    }
    if (colorUsed > colorArenaUploaded) {
        memcpy((u8*)(colorArena.contents) + colorArenaUploaded, (const u8*)(shapes.spanColor()) + colorArenaUploaded, colorUsed - colorArenaUploaded);
        colorArenaUploaded = colorUsed;
    }
    stripGeneration = generation;
    return true;
}

void MetalRendererImpl::applySpanStrips(u32 rowIndex, const ScreenRowSpan& span) {
    if (span.missing || span.end <= span.begin || span.end > cellColumns) {
        return;
    }
    const u32 stride = (u32)(span.end - span.begin) * composer.glyphWidth;
    for (u16 column = span.begin; column < span.end; ++column) {
        GpuCell& cell = cells.mut(rowIndex + column);
        cell.strip = (span.offset + (u32)(column - span.begin) * composer.glyphWidth) | (span.color ? stripColorPlane : 0);
        cell.stripStride = stride;
    }
}

void MetalRendererImpl::assignRowStrips(Screen& shapes, u16 row) {
    const u32 rowIndex = (u32)(row)*cellColumns;
    GpuCell* const rowCells = cells.mutData() + rowIndex;
    for (u16 column = 0; column < cellColumns; ++column) {
        rowCells[column].strip = stripNone;
        rowCells[column].stripStride = 0;
    }
    const size_t count = shapes.rowSpans(row, spanScratch.mutData());
    for (size_t index = 0; index < count; ++index) {
        applySpanStrips(rowIndex, spanScratch[index]);
    }
}

void MetalRendererImpl::overrideOverlayStrips(Screen& shapes, const TerminalUpdate& update) {
    if (update.overlaySpan == (size_t)-1 || update.overlaySpan >= update.spanCount) {
        return;
    }
    // The preedit preview covers the underlying strips wholesale: its
    // blank cells hide the text below them.
    const TerminalCellSpan& overlay = update.spans[update.overlaySpan];
    for (u32 index = 0; index < overlay.count; ++index) {
        GpuCell& cell = cells.mut(overlay.index + index);
        cell.strip = stripNone;
        cell.stripStride = 0;
    }
    const u32 rowIndex = (overlay.index / cellColumns) * cellColumns;
    const size_t count = shapes.shapeCells(overlay.cells, (u16)(overlay.count), (u16)(overlay.index % cellColumns), spanScratch.mutData());
    for (size_t index = 0; index < count; ++index) {
        applySpanStrips(rowIndex, spanScratch[index]);
    }
}

u32 MetalRendererImpl::assignStrips(const TerminalUpdate& update) {
    Screen& shapes = *update.shapes;
    spanScratch.clear();
    spanScratch.grow(cellColumns);
    while (spanScratch.length() < cellColumns) {
        spanScratch.pushBack({});
    }
    // A shaping pass can collect the arenas and move every strip assigned
    // so far, so redo the walk until it closes within one generation.
    u32 generation;
    do {
        generation = shapes.spanGeneration();
        for (u16 row = 0; row < cellRows; ++row) {
            assignRowStrips(shapes, row);
        }
        overrideOverlayStrips(shapes, update);
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
    metalLayer.drawableSize = CGSizeMake(width, height);
    outputWidth = width;
    outputHeight = height;
    currentFrame = 0;
    return true;
}

u32 MetalRendererImpl::buildCellUpdates(PresentationFrame& frame) {
    const u32 count = (u32)(cells.length());
    if (!ensureCellBuffer(frame, count)) {
        return 0;
    }
    auto* const updates = (GpuCellUpdate*)(frame.cellBuffer.contents);
    u32 updateCount = 0;
    for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex) {
        const u32 sourceRow = sourceIndex / cellColumns;
        const u32 sourceColumn = sourceIndex - sourceRow * cellColumns;
        const u32 rowIndex = sourceRow * cellColumns;
        const u8 lineAttribute = (u8)(cells[rowIndex].lineAttribute);
        if (lineAttribute == 0) {
            GpuCell cell = cells[sourceIndex];
            if ((cell.attributes & gpuDoubleWidth) != 0 && (sourceColumn + 1 >= cellColumns || (cells[sourceIndex + 1].attributes & gpuDoubleWidthContinuation) == 0)) {
                cell.attributes &= ~gpuDoubleWidth;
            }
            updates[updateCount++] = {sourceIndex, sourceIndex, cell};
            continue;
        }
        const u32 outputColumn = sourceColumn * 2;
        if (outputColumn >= cellColumns) {
            continue;
        }
        updates[updateCount++] = {sourceIndex, rowIndex + outputColumn, cells[sourceIndex]};
        if (outputColumn + 1 < cellColumns) {
            updates[updateCount++] = {sourceIndex, rowIndex + outputColumn + 1, cells[sourceIndex]};
        }
    }
    return updateCount;
}

u32 MetalRendererImpl::packColor(Color color) {
    return (u32)(color.red) | ((u32)(color.green) << 8) | ((u32)(color.blue) << 16);
}

void MetalRendererImpl::capture(const TerminalUpdate& update) {
    state.cursor = update.cursor;
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
    stateValid = true;
}

bool MetalRendererImpl::draw() {
    if (!ready || !stateValid || cells.empty() || outputWidth == 0 || outputHeight == 0) {
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
    id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
    if (drawable == nil) {
        return false;
    }
    id<MTLTexture> target = drawable.texture;
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

    const PushConstants constants{
        composer.glyphWidth,
        composer.glyphHeight,
        composer.columns,
        composer.rows,
        outputWidth,
        outputHeight,
        opts.border,
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
        opts.showWraps ? 1u : 0u,
        packColor(state.selectionForeground),
        packColor(state.selectionBackground),
        state.selectionColorMask,
        state.blinkVisible ? 1u : 0u,
        state.cursorBlink ? 1u : 0u,
        state.hoveredHyperlink,
        state.hoveredLinkBegin,
        state.hoveredLinkEnd,
        updateCount,
    };

    // The spirv-cross assignment for this shader: push constants at
    // buffer 0, the color arena at 1, the mask arena at 2, the cell
    // updates at 3, the drawable at texture 0.
    id<MTLComputeCommandEncoder> compute = [commandBuffer computeCommandEncoder];
    [compute setComputePipelineState:pipeline];
    [compute setBytes:&constants length:sizeof(constants) atIndex:0];
    [compute setBuffer:colorArena offset:0 atIndex:1];
    [compute setBuffer:maskArena offset:0 atIndex:2];
    [compute setBuffer:frame.cellBuffer offset:0 atIndex:3];
    [compute setTexture:target atIndex:0];
    [compute dispatchThreads:MTLSizeMake(updateCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [compute endEncoding];

    // presentsWithTransaction is set, so present synchronously: schedule the
    // command buffer, then present the drawable into the current CoreAnimation
    // transaction. draw() only ever runs on the main thread, which the layout
    // engine requires for an in-transaction present.
    [commandBuffer commit];
    [commandBuffer waitUntilScheduled];
    [drawable present];
    currentFrame = (currentFrame + 1) % framesInFlight;
    return true;
}

bool MetalRendererImpl::repaint() {
    return draw();
}

bool MetalRendererImpl::update(const TerminalUpdate& update) {
    if (!ready || update.colors == nullptr) {
        return false;
    }
    const u32 width = composer.pixelWidth;
    const u32 height = composer.pixelHeight;
    const size_t cellCount = (size_t)(composer.columns) * composer.rows;
    if (width == 0 || height == 0 || cellCount == 0 || !ensureTargets(width, height)) {
        return false;
    }

    const bool shapeChanged = cellColumns != composer.columns || cellRows != composer.rows || cells.length() != cellCount;
    if (shapeChanged) {
        size_t covered = 0;
        for (size_t index = 0; index < update.spanCount; ++index) {
            const TerminalCellSpan& span = update.spans[index];
            if (span.cells == nullptr || span.index != covered) {
                return false;
            }
            covered += span.count;
        }
        if (covered != cellCount) {
            return false;
        }
        cells.zero(cellCount);
        cellColumns = composer.columns;
        cellRows = composer.rows;
    }

    for (size_t index = 0; index < update.spanCount; ++index) {
        const TerminalCellSpan& span = update.spans[index];
        if (span.cells == nullptr || (size_t)(span.index) + span.count > cellCount || span.count > UINT16_MAX) {
            return false;
        }
        materializeCells(span.cells, cells.mutData() + span.index, (u16)(span.count), span.lineAttribute, *update.colors);
    }
    if (update.shapes != nullptr) {
        const u32 generation = assignStrips(update);
        if (!uploadArenas(*update.shapes, generation)) {
            return false;
        }
    }
    // The padding follows the live default background (OSC 11).
    clearBackground = update.colors->defaultBackground;
    capture(update);
    return draw();
}

Renderer* createMetalRenderer(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context) {
    if (context.backend != plt::RenderBackend::Cocoa || context.connection == nullptr) {
        return nullptr;
    }
    auto* const renderer = pool.make<MetalRendererImpl>(composer, (CAMetalLayer*)(context.connection));
    if (!renderer->initialize()) {
        return nullptr;
    }
    composer.fontChangedListeners.pushBack(pool.make<CallMetalFontChanged>(renderer));
    return renderer;
}
