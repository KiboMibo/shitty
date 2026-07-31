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
#include "unicode_map.h"
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
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CATransaction.h>

#undef Rect
#undef Point

#include <dispatch/dispatch.h>
#include <stdio.h>

using namespace stl;

@interface ShittyMetalPresenter: NSObject {
    CALayer* root_;
    u64 generation_;
}

- (instancetype)initWithRoot:(CALayer*)root;
- (u64)advance;
- (void)publish:(IOSurfaceRef)surface generation:(u64)generation;
- (void)invalidate;

@end

@implementation ShittyMetalPresenter

- (instancetype)initWithRoot:(CALayer*)root {
    self = [super init];
    if (self != nil) {
        root_ = [root retain];
        generation_ = 1;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        root_.contentsGravity = kCAGravityTopLeft;
        root_.magnificationFilter = kCAFilterNearest;
        root_.minificationFilter = kCAFilterNearest;
        [CATransaction commit];
    }
    return self;
}

- (void)dealloc {
    [root_ release];
    [super dealloc];
}

- (u64)advance {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
    return generation_;
}

- (void)publish:(IOSurfaceRef)surface generation:(u64)generation {
    if (generation != generation_ || surface == nullptr) {
        return;
    }
    const CGSize bounds = root_.bounds.size;
    const CGFloat scale = root_.contentsScale;
    const size_t width = (size_t)(bounds.width * scale + 0.5);
    const size_t height = (size_t)(bounds.height * scale + 0.5);
    if (IOSurfaceGetWidth(surface) != width || IOSurfaceGetHeight(surface) != height) {
        return;
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    root_.contents = (id)(surface);
    [CATransaction commit];
}

- (void)invalidate {
    [self advance];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    root_.contents = nil;
    [CATransaction commit];
}

@end

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

    struct GpuCell {
        u32 codepoint = ' ';
        u32 attributes = 0;
        u32 foreground = 0;
        u32 background = 0;
        u32 underlineColor = 0;
        u32 hyperlink = 0;
        u32 glyph = 0;
        u32 semantic = 0;
        u32 lineAttribute = 0;
    };

    static_assert(sizeof(GpuCell) == 36, "Metal cell layout mismatch");

    struct GpuCellUpdate {
        u32 sourceIndex;
        u32 outputIndex;
        GpuCell cell;
    };

    static_assert(sizeof(GpuCellUpdate) == 44, "Metal cell update layout mismatch");

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
        u32 hasDoubleWidth;
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

    static_assert(sizeof(PushConstants) == 112, "Metal push constant layout mismatch");

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

    struct GlyphSlot {
        u32 id = 0;
        u32 generation = 0;
        u8 layers = 0;
        u8 colorLayers = 0;
        bool grapheme = false;
    };

    struct GlyphCache {
        explicit GlyphCache(ObjPool& pool);

        UnicodeMap<u16>* refs;
        Buffer graphemeRefs;
        Vector<GlyphSlot> slots;
        u32 columns = 0;
        u32 rows = 0;
        u32 next = 1;
        u32 eviction = 1;
        u32 generation = 0;
    };

    struct PresentationFrame {
        IOSurfaceRef surface = nullptr;
        id<MTLTexture> texture = nil;
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

    struct CallMetalCellExtrasChanged final: public Listener {
        explicit CallMetalCellExtrasChanged(MetalRendererImpl* renderer);

        void onListen(void*) override;

        MetalRendererImpl* renderer;
    };

    struct MetalRendererImpl final: public Renderer {
        MetalRendererImpl(Composer& composer, CALayer* root);
        ~MetalRendererImpl();

        bool initialize();
        bool update(const TerminalUpdate& update) override;
        bool repaint() override;

        void resetFontResources();
        bool buildFontResources();
        void growGlyphAtlas();
        bool configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget);
        id<MTLTexture> createAtlas(MTLPixelFormat format, u32 width, u32 height, u32 layers);
        bool ensureColorAtlas(bool doubleWidth);
        void beginGlyphFrame();
        void pinVisibleGlyphs();
        u16 allocateGlyphSlot(GlyphCache& cache, u32 id, bool grapheme);
        u32 ensureGlyph(Fontpack& fonts, const u32* codepoints, size_t count, u32 glyphId, bool grapheme, FontStyle style, bool doubleWidth);
        void materializeCells(const TerminalCell* input, GpuCell* output, u16 count, u8 lineAttribute, const TerminalColors& colors);
        bool ensureTargets(u32 width, u32 height);
        bool ensureCellBuffer(PresentationFrame& frame, size_t count);
        u32 buildCellUpdates(PresentationFrame& frame);
        bool draw();
        void waitFrames();
        void destroyTargets();
        void destroyFontResources();
        void capture(const TerminalUpdate& update);
        static bool needsFontGlyph(u32 id);
        static u32 packColor(Color color);

        Composer& composer;
        CALayer* root;
        ShittyMetalPresenter* presenter = nil;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLComputePipelineState> pipeline = nil;
        id<MTLSamplerState> sampler = nil;
        id<MTLTexture> output = nil;
        id<MTLTexture> atlas = nil;
        id<MTLTexture> colorAtlas = nil;
        id<MTLTexture> doubleWidthAtlas = nil;
        id<MTLTexture> doubleWidthColorAtlas = nil;
        id<MTLTexture> emptyMask = nil;
        id<MTLTexture> emptyColor = nil;
        MTLStorageMode textureStorageMode = MTLStorageModeShared;
        Color clearBackground = opts.bg;
        PresentationFrame frames[framesInFlight];
        u32 currentFrame = 0;
        u32 outputWidth = 0;
        u32 outputHeight = 0;
        ObjPool::Ref glyphPool;
        GlyphCache* glyphs = nullptr;
        GlyphCache* doubleWidthGlyphs = nullptr;
        Vector<GpuCell> cells;
        Buffer emptyGlyph;
        // Nonzero once an overflow resized the caches to the working set;
        // overrides the byte-budget slot count from then on.
        u32 glyphSlotTarget = 0;
        bool atlasExhausted = false;
        u16 cellColumns = 0;
        u16 cellRows = 0;
        PresentationState state;
        bool stateValid = false;
        bool synchronousFrame = false;
        bool ready = false;
    };

}

GlyphCache::GlyphCache(ObjPool& pool)
    : refs(UnicodeMap<u16>::create(pool))
{
}

CallMetalFontChanged::CallMetalFontChanged(MetalRendererImpl* renderer_)
    : renderer(renderer_)
{
}

void CallMetalFontChanged::onListen(void*) {
    renderer->resetFontResources();
}

CallMetalCellExtrasChanged::CallMetalCellExtrasChanged(MetalRendererImpl* renderer_)
    : renderer(renderer_)
{
}

void CallMetalCellExtrasChanged::onListen(void*) {
    renderer->resetFontResources();
}

MetalRendererImpl::MetalRendererImpl(Composer& composer_, CALayer* root_)
    : composer(composer_)
    , root([root_ retain])
    , glyphPool(ObjPool::fromMemory())
{
}

MetalRendererImpl::~MetalRendererImpl() {
    waitFrames();
    if (presenter != nil) {
        [presenter invalidate];
    }
    destroyTargets();
    destroyFontResources();
    for (PresentationFrame& frame : frames) {
        [frame.cellBuffer release];
        frame.cellBuffer = nil;
        frame.cellCapacity = 0;
    }
    [emptyColor release];
    [emptyMask release];
    [sampler release];
    [pipeline release];
    [queue release];
    [device release];
    [presenter release];
    [root release];
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

    MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
    [samplerDescriptor release];
    if (sampler == nil || !buildFontResources()) {
        return false;
    }

    emptyMask = createAtlas(MTLPixelFormatR8Unorm, 1, 1, 1);
    emptyColor = createAtlas(MTLPixelFormatRGBA8Unorm, 1, 1, 1);
    if (emptyMask == nil || emptyColor == nil) {
        return false;
    }
    const u8 zeroMask = 0;
    const u32 zeroColor = 0;
    [emptyMask replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:0 withBytes:&zeroMask bytesPerRow:1 bytesPerImage:1];
    [emptyColor replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:0 withBytes:&zeroColor bytesPerRow:4 bytesPerImage:4];

    presenter = [[ShittyMetalPresenter alloc] initWithRoot:root];
    ready = presenter != nil;
    return ready;
}

bool MetalRendererImpl::configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget) {
    constexpr u32 maximumSlots = 16384;
    const size_t glyphBytes = (size_t)(width)*composer.glyphHeight * layers;
    u32 requested = glyphBytes == 0 ? 2 : (u32)(byteBudget / glyphBytes);
    requested = min(max(requested, 2u), maximumSlots);
    if (glyphSlotTarget != 0) {
        // Resized after an overflow: the working set of the screen and its
        // scrollback dictates the capacity, not the byte budget.
        requested = glyphSlotTarget;
    }

    constexpr u32 maximumTextureDimension = 8192;
    u32 maximumColumns = min(maximumTextureDimension / width, 256u);
    u32 maximumRows = min(maximumTextureDimension / composer.glyphHeight, 256u);
    if (maximumColumns == 0 || maximumRows == 0) {
        return false;
    }
    requested = min(requested, maximumColumns * maximumRows);

    u32 bestColumns = 0;
    u32 bestRows = 0;
    u64 bestDifference = UINT64_MAX;
    for (u32 columns = 1; columns <= maximumColumns; ++columns) {
        const u32 rows = (requested + columns - 1) / columns;
        if (rows > maximumRows) {
            continue;
        }
        const u64 pixelWidth = (u64)(columns)*width;
        const u64 pixelHeight = (u64)(rows)*composer.glyphHeight;
        const u64 difference = pixelWidth > pixelHeight ? pixelWidth - pixelHeight : pixelHeight - pixelWidth;
        if (difference < bestDifference) {
            bestDifference = difference;
            bestColumns = columns;
            bestRows = rows;
        }
    }
    if (bestColumns == 0 || bestRows == 0) {
        return false;
    }
    cache.columns = bestColumns;
    cache.rows = bestRows;
    cache.slots.zero((size_t)(bestColumns)*bestRows);
    return true;
}

id<MTLTexture> MetalRendererImpl::createAtlas(MTLPixelFormat format, u32 width, u32 height, u32 layers) {
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2DArray;
    descriptor.pixelFormat = format;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.arrayLength = layers;
    descriptor.mipmapLevelCount = 1;
    descriptor.storageMode = textureStorageMode;
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    [descriptor release];
    return texture;
}

bool MetalRendererImpl::buildFontResources() {
    constexpr size_t atlasByteBudget = 16 * 1024 * 1024;
    constexpr size_t doubleWidthAtlasByteBudget = 8 * 1024 * 1024;
    ObjPool::Ref replacement = ObjPool::fromMemory();
    GlyphCache* const replacementGlyphs = replacement->make<GlyphCache>(*replacement);
    GlyphCache* const replacementDoubleWidthGlyphs = replacement->make<GlyphCache>(*replacement);

    if (!configureGlyphCache(*replacementGlyphs, composer.glyphWidth, 4, atlasByteBudget)) {
        return false;
    }
    id<MTLTexture> replacementAtlas = createAtlas(MTLPixelFormatR8Unorm, composer.glyphWidth * replacementGlyphs->columns, composer.glyphHeight * replacementGlyphs->rows, 4);
    if (replacementAtlas == nil) {
        return false;
    }

    id<MTLTexture> replacementDoubleWidthAtlas = nil;
    if (!configureGlyphCache(*replacementDoubleWidthGlyphs, 2 * composer.glyphWidth, 1, doubleWidthAtlasByteBudget)) {
        [replacementAtlas release];
        return false;
    }
    replacementDoubleWidthAtlas = createAtlas(MTLPixelFormatR8Unorm, 2 * composer.glyphWidth * replacementDoubleWidthGlyphs->columns, composer.glyphHeight * replacementDoubleWidthGlyphs->rows, 1);
    if (replacementDoubleWidthAtlas == nil) {
        [replacementAtlas release];
        return false;
    }

    [atlas release];
    [colorAtlas release];
    [doubleWidthAtlas release];
    [doubleWidthColorAtlas release];
    atlas = replacementAtlas;
    colorAtlas = nil;
    doubleWidthAtlas = replacementDoubleWidthAtlas;
    doubleWidthColorAtlas = nil;
    glyphPool.xchg(replacement);
    glyphs = replacementGlyphs;
    doubleWidthGlyphs = replacementDoubleWidthGlyphs;
    return true;
}

void MetalRendererImpl::destroyFontResources() {
    [doubleWidthColorAtlas release];
    [doubleWidthAtlas release];
    [colorAtlas release];
    [atlas release];
    doubleWidthColorAtlas = nil;
    doubleWidthAtlas = nil;
    colorAtlas = nil;
    atlas = nil;
}

void MetalRendererImpl::resetFontResources() {
    waitFrames();
    if (!buildFontResources()) {
        ready = false;
        return;
    }
    cells.clear();
    cellColumns = 0;
    cellRows = 0;
    stateValid = false;
}

bool MetalRendererImpl::ensureColorAtlas(bool doubleWidth) {
    id<MTLTexture>& texture = doubleWidth ? doubleWidthColorAtlas : colorAtlas;
    if (texture != nil) {
        return true;
    }
    GlyphCache& cache = doubleWidth ? *doubleWidthGlyphs : *glyphs;
    const u32 glyphWidth = composer.glyphWidth * (doubleWidth ? 2 : 1);
    texture = createAtlas(MTLPixelFormatRGBA8Unorm, glyphWidth * cache.columns, composer.glyphHeight * cache.rows, doubleWidth ? 1 : 4);
    return texture != nil;
}

void MetalRendererImpl::beginGlyphFrame() {
    const auto advance = [](GlyphCache& cache) {
        ++cache.generation;
        if (cache.generation == 0) {
            for (GlyphSlot* slot = cache.slots.mutBegin(); slot != cache.slots.mutEnd(); ++slot) {
                slot->generation = 0;
            }
            cache.generation = 1;
        }
    };
    advance(*glyphs);
    if (doubleWidthAtlas != nil) {
        advance(*doubleWidthGlyphs);
    }
}

void MetalRendererImpl::pinVisibleGlyphs() {
    for (GpuCell* cell = cells.mutBegin(); cell != cells.mutEnd(); ++cell) {
        if (cell->glyph == 0) {
            continue;
        }
        GlyphCache& cache = cell->lineAttribute != 0 || (cell->attributes & gpuDoubleWidth) != 0 ? *doubleWidthGlyphs : *glyphs;
        const u32 slot = (cell->glyph & 0xff) + (((cell->glyph >> 8) & 0xff) * cache.columns);
        if (slot < cache.slots.length()) {
            cache.slots.mut(slot).generation = cache.generation;
        }
    }
}

u16 MetalRendererImpl::allocateGlyphSlot(GlyphCache& cache, u32 id, bool grapheme) {
    if (cache.next < cache.slots.length()) {
        const u16 slot = (u16)(cache.next++);
        GlyphSlot& state = cache.slots.mut(slot);
        state.id = id;
        state.generation = cache.generation;
        state.layers = 0;
        state.colorLayers = 0;
        state.grapheme = grapheme;
        return slot;
    }

    const u32 count = cache.slots.length();
    for (u32 checked = 1; checked < count; ++checked) {
        if (cache.eviction == 0 || cache.eviction >= count) {
            cache.eviction = 1;
        }
        const u16 slot = (u16)(cache.eviction++);
        GlyphSlot& state = cache.slots.mut(slot);
        if (state.generation == cache.generation) {
            continue;
        }
        if (state.grapheme) {
            const size_t offset = (size_t)(state.id) * sizeof(u16);
            if (offset + sizeof(u16) <= cache.graphemeRefs.used()) {
                u16* const ref = (u16*)((u8*)(cache.graphemeRefs.mutData()) + offset);
                if (*ref == slot) {
                    *ref = 0;
                }
            }
        } else if (u16* const ref = cache.refs->find(state.id); ref != nullptr && *ref == slot) {
            *ref = 0;
        }
        state.id = id;
        state.generation = cache.generation;
        state.layers = 0;
        state.colorLayers = 0;
        state.grapheme = grapheme;
        return slot;
    }
    // Every slot is pinned by the current frame: remember to resize the
    // caches to the working set before the next frame.
    atlasExhausted = true;
    return 0;
}

void MetalRendererImpl::growGlyphAtlas() {
    // Twice the distinct glyphs reachable on screen and in scrollback:
    // bounded by live content rather than by a doubling history, and free
    // to shrink back once the content simplifies.
    u64 target = composer.vterm != nullptr ? 2 * (u64)(composer.vterm->distinctGlyphs()) : 2 * (u64)(glyphs->slots.length());
    if (target > 65536) {
        target = 65536;
    }
    glyphSlotTarget = (u32)(target);
    resetFontResources();
}

bool MetalRendererImpl::needsFontGlyph(u32 id) {
    return id != 0x200d && !(id >= 0xfe00 && id <= 0xfe0f) && !(id >= 0xe0100 && id <= 0xe01ef) && !(id >= 0x2500 && id <= 0x257f) && !(id >= 0x23ba && id <= 0x23bd);
}

u32 MetalRendererImpl::ensureGlyph(Fontpack& fonts, const u32* codepoints, size_t count, u32 glyphId, bool grapheme, FontStyle style, bool doubleWidth) {
    if (count == 0 || (!grapheme && (glyphId >= 0x110000 || !needsFontGlyph(glyphId)))) {
        return 0;
    }

    GlyphCache& cache = doubleWidth ? *doubleWidthGlyphs : *glyphs;
    u16* ref = nullptr;
    if (grapheme) {
        const size_t required = ((size_t)(glyphId) + 1) * sizeof(u16);
        if (required > cache.graphemeRefs.used()) {
            const size_t previous = cache.graphemeRefs.used();
            cache.graphemeRefs.grow(required);
            cache.graphemeRefs.seekAbsolute(required);
            memZero((u8*)(cache.graphemeRefs.mutData()) + previous, cache.graphemeRefs.mutCurrent());
        }
        ref = (u16*)((u8*)(cache.graphemeRefs.mutData()) + (size_t)(glyphId) * sizeof(u16));
    } else {
        ref = &(*cache.refs)[glyphId];
    }
    u16 slot = *ref;
    if (slot != 0) {
        const GlyphSlot& state = cache.slots[slot];
        if (state.id != glyphId || state.grapheme != grapheme) {
            slot = 0;
            *ref = 0;
        }
    }
    if (slot == 0) {
        slot = allocateGlyphSlot(cache, glyphId, grapheme);
        if (slot == 0) {
            return 0;
        }
        *ref = slot;
    }

    GlyphSlot& state = cache.slots.mut(slot);
    state.generation = cache.generation;
    const u32 layer = doubleWidth ? 0 : (u32)(style);
    const u8 layerMask = (u8)(1u << layer);
    if (state.layers & layerMask) {
        return (slot % cache.columns) | ((slot / cache.columns) << 8) | ((u32)((state.colorLayers & layerMask) != 0) << 16);
    }

    const u32 width = composer.glyphWidth * (doubleWidth ? 2 : 1);
    const FontGlyph glyph = fonts.glyph(codepoints, count, style, doubleWidth);
    const size_t bytesPerPixel = glyph.color ? 4 : 1;
    const size_t bytesPerRow = (size_t)(width)*bytesPerPixel;
    const size_t expected = bytesPerRow * composer.glyphHeight;
    const void* data = glyph.data;
    if (glyph.len != expected) {
        emptyGlyph.zero(expected);
        data = emptyGlyph.data();
    }
    if (glyph.color && !ensureColorAtlas(doubleWidth)) {
        return 0;
    }
    id<MTLTexture> texture = glyph.color ? (doubleWidth ? doubleWidthColorAtlas : colorAtlas) : (doubleWidth ? doubleWidthAtlas : atlas);
    [texture replaceRegion:MTLRegionMake2D((slot % cache.columns) * width, (slot / cache.columns) * composer.glyphHeight, width, composer.glyphHeight) mipmapLevel:0 slice:layer withBytes:data bytesPerRow:bytesPerRow bytesPerImage:expected];
    state.layers |= layerMask;
    if (glyph.color) {
        state.colorLayers |= layerMask;
    }
    return (slot % cache.columns) | ((slot / cache.columns) << 8) | ((u32)(glyph.color) << 16);
}

void MetalRendererImpl::materializeCells(const TerminalCell* input, GpuCell* outputCells, u16 count, u8 lineAttribute, const TerminalColors& colors) {
    CellExtraStore& extras = *composer.cellExtras;
    Fontpack& fonts = *composer.fonts;
    const bool specialColors = colors.specialModes != 0;
    for (u16 index = 0; index < count; ++index) {
        const TerminalCell& cell = input[index];
        const u32 codepoint = cell.uc_pt ? cell.uc_pt : ' ';
        const u32 attributes = cellAttributes(cell);
        const u32 foreground = specialColors ? colors.resolveForegroundSpecial(cell).packed() : colors.resolvePacked(cell.foreground());
        const u32 background = specialColors ? colors.resolveBackgroundSpecial(cell).packed() : colors.resolvePacked(cell.background());
        u32 underlineColor = foreground;
        u32 hyperlink = 0;
        u32 graphemeId = 0;
        GraphemeView grapheme;
        if (cell.hasExtra()) {
            const CellExtraView extra = extras.view(cell);
            hyperlink = extra.hyperlinkDisplayId;
            grapheme = extra.grapheme;
            graphemeId = grapheme.empty() ? 0 : cell.extraRef();
            if (cell.underlined() && extra.underlineColor != cell.foreground()) {
                underlineColor = colors.resolvePacked(extra.underlineColor);
            }
        } else if (cell.underlined() && cell.inlineUnderlineColor() != cell.foreground()) {
            underlineColor = colors.resolvePacked(cell.inlineUnderlineColor());
        }

        u32 glyph = 0;
        const bool doubleWidth = lineAttribute != 0 || cell.dwidth;
        if (!cell.dwidth_cont || lineAttribute != 0) {
            const FontStyle style = (FontStyle)((cell.bold ? 1 : 0) | (cell.italic ? 2 : 0));
            glyph = graphemeId != 0 ? ensureGlyph(fonts, grapheme.data(), grapheme.size(), graphemeId, true, style, doubleWidth) : ensureGlyph(fonts, &codepoint, 1, codepoint, false, style, doubleWidth);
        }
        outputCells[index] = {
            codepoint,
            attributes,
            foreground,
            background,
            underlineColor,
            hyperlink,
            glyph,
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
    for (PresentationFrame& frame : frames) {
        [frame.texture release];
        frame.texture = nil;
        if (frame.surface != nullptr) {
            CFRelease(frame.surface);
            frame.surface = nullptr;
        }
    }
    [output release];
    output = nil;
    outputWidth = 0;
    outputHeight = 0;
}

bool MetalRendererImpl::ensureTargets(u32 width, u32 height) {
    if (output != nil && outputWidth == width && outputHeight == height) {
        return true;
    }
    destroyTargets();

    MTLTextureDescriptor* outputDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
    outputDescriptor.storageMode = MTLStorageModePrivate;
    outputDescriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    output = [device newTextureWithDescriptor:outputDescriptor];
    if (output == nil) {
        return false;
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (colorSpace == nullptr) {
        destroyTargets();
        return false;
    }
    CFPropertyListRef serializedColorSpace = CGColorSpaceCopyPropertyList(colorSpace);
    CGColorSpaceRelease(colorSpace);
    if (serializedColorSpace == nullptr) {
        destroyTargets();
        return false;
    }

    const size_t bytesPerRow = ((size_t)(width) * 4 + 63) & ~(size_t)(63);
    for (PresentationFrame& frame : frames) {
        NSDictionary* properties = @{
            (id)(kIOSurfaceWidth) : @(width),
            (id)(kIOSurfaceHeight) : @(height),
            (id)(kIOSurfaceBytesPerElement) : @4,
            (id)(kIOSurfaceBytesPerRow) : @(bytesPerRow),
            (id)(kIOSurfacePixelFormat) : @(0x42475241u),
            (id)(kIOSurfaceIsGlobal) : @NO,
        };
        frame.surface = IOSurfaceCreate((CFDictionaryRef)(properties));
        if (frame.surface == nullptr) {
            CFRelease(serializedColorSpace);
            destroyTargets();
            return false;
        }
        IOSurfaceSetValue(frame.surface, kIOSurfaceColorSpace, serializedColorSpace);
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
        descriptor.storageMode = textureStorageMode;
        descriptor.usage = MTLTextureUsageShaderRead;
        frame.texture = [device newTextureWithDescriptor:descriptor iosurface:frame.surface plane:0];
        if (frame.texture == nil) {
            CFRelease(serializedColorSpace);
            destroyTargets();
            return false;
        }
    }
    CFRelease(serializedColorSpace);
    outputWidth = width;
    outputHeight = height;
    currentFrame = 0;
    synchronousFrame = true;
    [presenter advance];
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
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (commandBuffer == nil) {
        return false;
    }
    [commandBuffer retain];
    frame.commandBuffer = commandBuffer;

    MTLRenderPassDescriptor* clearPass = [MTLRenderPassDescriptor renderPassDescriptor];
    clearPass.colorAttachments[0].texture = output;
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
        doubleWidthAtlas != nil ? 1u : 0u,
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

    id<MTLComputeCommandEncoder> compute = [commandBuffer computeCommandEncoder];
    [compute setComputePipelineState:pipeline];
    [compute setBytes:&constants length:sizeof(constants) atIndex:0];
    [compute setBuffer:frame.cellBuffer offset:0 atIndex:1];
    [compute setTexture:output atIndex:0];
    [compute setTexture:colorAtlas != nil ? colorAtlas : emptyColor atIndex:1];
    [compute setTexture:doubleWidthColorAtlas != nil ? doubleWidthColorAtlas : emptyColor atIndex:2];
    [compute setTexture:atlas atIndex:3];
    [compute setTexture:doubleWidthAtlas != nil ? doubleWidthAtlas : emptyMask atIndex:4];
    for (u32 index = 0; index < 4; ++index) {
        [compute setSamplerState:sampler atIndex:index];
    }
    [compute dispatchThreads:MTLSizeMake(updateCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [compute endEncoding];

    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(outputWidth, outputHeight, 1) toTexture:frame.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];

    const u64 generation = [presenter advance];
    if (synchronousFrame) {
        synchronousFrame = false;
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status == MTLCommandBufferStatusCompleted) {
            [presenter publish:frame.surface generation:generation];
        }
    } else {
        IOSurfaceRef const surface = frame.surface;
        CFRetain(surface);
        ShittyMetalPresenter* const target = [presenter retain];
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
          const bool success = completed.status == MTLCommandBufferStatusCompleted;
          dispatch_async(dispatch_get_main_queue(), ^{
            if (success) {
                [target publish:surface generation:generation];
            }
            CFRelease(surface);
            [target release];
          });
        }];
        [commandBuffer commit];
    }
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
    if (atlasExhausted) {
        atlasExhausted = false;
        growGlyphAtlas();
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

    beginGlyphFrame();
    if (!shapeChanged) {
        pinVisibleGlyphs();
    }
    for (size_t index = 0; index < update.spanCount; ++index) {
        const TerminalCellSpan& span = update.spans[index];
        if (span.cells == nullptr || (size_t)(span.index) + span.count > cellCount || span.count > UINT16_MAX) {
            return false;
        }
        materializeCells(span.cells, cells.mutData() + span.index, (u16)(span.count), span.lineAttribute, *update.colors);
    }
    // The padding follows the live default background (OSC 11).
    clearBackground = update.colors->defaultBackground;
    capture(update);
    const bool drawn = draw();
    if (atlasExhausted && composer.vterm != nullptr) {
        // The frame just presented is missing the glyphs that did not
        // fit; ask for a full redraw, which lands after growGlyphAtlas.
        composer.vterm->expose();
    }
    return drawn;
}

Renderer* createMetalRenderer(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context) {
    if (context.backend != plt::RenderBackend::Cocoa || context.connection == nullptr) {
        return nullptr;
    }
    auto* const renderer = pool.make<MetalRendererImpl>(composer, (CALayer*)(context.connection));
    if (!renderer->initialize()) {
        return nullptr;
    }
    composer.fontChangedListeners.pushBack(pool.make<CallMetalFontChanged>(renderer));
    composer.cellExtrasChangedListeners.pushBack(pool.make<CallMetalCellExtrasChanged>(renderer));
    return renderer;
}
