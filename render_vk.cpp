/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_vk.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "listener.h"
#include "render.h"

#include "options.h"
#include "render_damage.h"
#include "unicode_map.h"
#include "utf8.h"
#include "vterm.h"

#include <plt/window.h>

#include <std/dbg/assert.h>
#include <std/alg/xchg.h>
#include <std/sys/crt.h>
#include <std/ios/sys.h>
#include <std/lib/buffer.h>
#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/mem/new.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>
#include <std/rng/mix.h>
#include <std/str/hash.h>
#include <std/str/view.h>
#include <std/typ/intrin.h>

#include <vulkan/vulkan.h>

#if defined(HAVE_VULKAN_WAYLAND)
    #include <vulkan/vulkan_wayland.h>
#else
    #error No Vulkan window-system backend selected
#endif

#include "render_spv.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stl;

namespace {
    struct RendererImpl;

    struct CallRendererFontChanged final: public Listener {
        explicit CallRendererFontChanged(RendererImpl* renderer);

        void onListen(void*) override;

        RendererImpl* renderer;
    };

    struct CallRendererCellExtrasChanged final: public Listener {
        explicit CallRendererCellExtrasChanged(RendererImpl* renderer);

        void onListen(void*) override;

        RendererImpl* renderer;
    };

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

    static_assert(sizeof(GpuCell) == 36, "Vulkan cell layout mismatch");

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

    constexpr size_t renderCacheBytes = 1000000;
    constexpr u16 renderCacheChunkCells = 32;

    struct RenderCacheBlock final: public IntrusiveNode, public Newable {
        u64 hash = 0;
        GpuCell cells[renderCacheChunkCells];
    };

    constexpr size_t renderCacheBlockCount = renderCacheBytes / sizeof(RenderCacheBlock);

    constexpr size_t renderCacheBucketCount() {
        size_t result = 1;
        while (result < renderCacheBlockCount * 2) {
            result <<= 1;
        }
        return result;
    }

    struct RenderCache {
        RenderCache();

        void render(RendererImpl& renderer, const TerminalCell* cells, u16 count, u8 lineAttribute, GpuCell* output, const TerminalColors& colors, u64 context);

        static constexpr size_t bucketCount = renderCacheBucketCount();
        static constexpr size_t bucketMask = bucketCount - 1;

        Buffer storage;
        Vector<RenderCacheBlock*> freeBlocks;
        IntrusiveList lru;
        RenderCacheBlock* buckets[bucketCount]{};
    };

    static_assert(renderCacheBlockCount != 0);
    static_assert(stdHasTrivialDestructor(RenderCacheBlock));

    struct GpuCellUpdate {
        u32 sourceIndex;
        u32 outputIndex;
        GpuCell cell;
    };

    static_assert(sizeof(GpuCellUpdate) == 44, "Vulkan cell update layout mismatch");

    struct RendererImpl final: public Renderer {
        RendererImpl(Composer& composer, const plt::RenderContext& context);
        ~RendererImpl();

        bool update(const TerminalUpdate& update) override;
        bool repaint() override;
        bool repaintFrame();

        struct ImageResource {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            u32 width = 0;
            u32 height = 0;
            u32 layers = 0;
        };

        struct FrameResources {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkBuffer cellBuffer = VK_NULL_HANDLE;
            VkDeviceMemory cellMemory = VK_NULL_HANDLE;
            void* cells = nullptr;
            size_t cellCapacity = 0;
            VkBuffer fontUploadBuffer = VK_NULL_HANDLE;
            VkDeviceMemory fontUploadMemory = VK_NULL_HANDLE;
            void* fontUploads = nullptr;
            size_t fontUploadCapacity = 0;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
        };

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

        static_assert(sizeof(PushConstants) == 112, "Vulkan push constant layout mismatch");

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

        struct FontResources {
            FontResources();

            ObjPool::Ref pool;
            ImageResource atlas;
            ImageResource colorAtlas;
            ImageResource doubleWidthAtlas;
            ImageResource doubleWidthColorAtlas;
            GlyphCache glyphs;
            GlyphCache doubleWidthGlyphs;
        };

        struct SwapchainResources {
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkFormat storageViewFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D extent{};
            Vector<VkImage> images;
            Vector<VkImageView> views;
            Vector<VkSemaphore> semaphores;
            Vector<VkFence> presentFences;
            Vector<u8> presentFencePending;
            Vector<u8> initialized;
            Vector<u64> generations;
            ImageResource output;
            bool outputInitialized = false;
            u64 outputGeneration = 0;
            bool direct = false;
            // Without swapchain maintenance there is no presentation fence:
            // destroy after this many further presented frames instead of
            // piling retirees up to a device-wide wait.
            u32 gracePresents = 0;
        };

        struct PresentTarget {
            VkSurfaceFormatKHR format{};
            const GeneratedRenderShader* shader = nullptr;
            bool direct = false;
        };

        struct PresentationState {
            TerminalCursor cursor;
            Rect selection;
            Color selectionForeground;
            Color selectionBackground;
            u32 selectionColorMask = 0;
            u32 hoveredHyperlink = 0;
            u32 hoveredLinkBegin = 0;
            u32 hoveredLinkEnd = 0;
            bool screenReverse = false;
            bool blinkVisible = false;
            bool cursorBlink = false;
        };

        static constexpr u32 framesInFlight = 2;

        Composer& composer;

        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        u32 queueFamily = UINT32_MAX;
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const GeneratedRenderShader* activeShader = nullptr;
        VkSampler atlasSampler = VK_NULL_HANDLE;

        // The live font/glyph state; never null after construction.
        // resetFontResources installs a replacement by pointer swap.
        FontResources* fontResources = nullptr;
        bool atlasInitialized = false;
        bool colorAtlasInitialized = false;
        bool doubleWidthAtlasInitialized = false;
        bool doubleWidthColorAtlasInitialized = false;
        Buffer fontUploadData;
        Buffer updateEpochs;
        u32 updateEpoch = 0;
        Buffer damageJournalStorage;
        RenderDamage damage;
        u64 clearDamageGeneration = 0;
        Vector<GpuCell> cells;
        RenderCache renderCache;
        Vector<VkBufferImageCopy> atlasCopies;
        Vector<VkBufferImageCopy> colorAtlasCopies;
        Vector<VkBufferImageCopy> doubleWidthAtlasCopies;
        Vector<VkBufferImageCopy> doubleWidthColorAtlasCopies;
        TerminalCursor previousCursor;
        Rect previousSelection;
        Color clearBackground = opts.bg;
        u32 previousHoveredHyperlink = 0;
        u32 previousHoveredLinkBegin = 0;
        u32 previousHoveredLinkEnd = 0;
        bool previousStateValid = false;
        PresentationState presentationState;
        u16 cellColumns = 0;
        u16 cellRows = 0;
        bool mutableSwapchainFormats = false;
        bool extendedStorageFormats = false;
        bool khrSurfaceMaintenance = false;
        bool extSurfaceMaintenance = false;
        const char* swapchainMaintenanceExtension = nullptr;

        // The live presentation chain; never null. Replaced wholesale by
        // createSwapchain, which retires the previous instance.
        SwapchainResources* chain = nullptr;
        VkExtent2D renderExtent{};
        Vector<SwapchainResources*> retiredSwapchains;
        Vector<VkPipeline> retiredPipelines;

        std::array<FrameResources, framesInFlight> frames;
        u32 currentFrame = 0;
        void createInstance();
        void createSurface(const plt::RenderContext& context);
        void selectPhysicalDevice();
        void createDevice();
        void createCommandResources();
        void createFontResources();
        FontResources* buildFontResources();
        void destroyFontResources(FontResources& resources);
        void resetFontResources();
        void cellExtrasChanged();
        void createDescriptors();
        void createPipelineLayout();
        void selectPipeline(const GeneratedRenderShader& shader);
        PresentTarget selectPresentTarget(const VkSurfaceCapabilitiesKHR& capabilities, const std::vector<VkSurfaceFormatKHR>& formats) const;
        void createSwapchain(u32 width, u32 height);
        bool tryCreateSwapchain(u32 width, u32 height);
        void destroySwapchainResources(SwapchainResources& resources);
        void retireSwapchain(SwapchainResources* resources);
        void collectRetiredSwapchains(bool force = false);
        void ensureCellBuffer(FrameResources& frame, size_t bytes);
        void ensureFontUploadBuffer(FrameResources& frame, size_t bytes);
        void releaseBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped);
        void ensureColorAtlas(bool doubleWidth);

        ImageResource createImage(u32 width, u32 height, u32 layers, VkFormat format, VkImageUsageFlags usage, bool arrayView = false, VkFormat viewFormat = VK_FORMAT_UNDEFINED);
        void destroyImage(ImageResource& image);
        VkImageView createImageView(VkImage image, VkFormat format) const;
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;
        u32 findMemoryType(u32 allowed, VkMemoryPropertyFlags properties) const;
        void updateStaticDescriptors();
        void updateOutputDescriptor(FrameResources& frame, VkImageView view);
        void updateCellDescriptor(FrameResources& frame);
        void beginGlyphFrame();
        void pinVisibleGlyphs();
        void configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget, u32 maxImageDimension);
        u16 allocateGlyphSlot(GlyphCache& cache, u32 id, bool grapheme);
        u32 ensureGlyph(Fontpack& fonts, const u32* codepoints, size_t count, u32 id, bool grapheme, FontStyle style, bool doubleWidth);
        VkDeviceSize stageFontData(const void* data, size_t len, size_t expected);
        void recordFontUploads(FrameResources& frame);
        void recordImageUploads(VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, const ImageResource& image, const Vector<VkBufferImageCopy>& copies, bool initialize);
        void recordCommands(FrameResources& frame, u32 imageIndex, const PresentationState& state, u32 updateCount, bool clearOutput);
        void recordRepaintCommands(FrameResources& frame, u32 imageIndex);
        void recordBlit(FrameResources& frame, u32 imageIndex, VkAccessFlags outputSrcAccess, VkPipelineStageFlags outputSrcStage);
        void recordFrame(FrameResources& frame, u32 imageIndex);
        bool acquirePresentFrame(u32 width, u32 height, FrameResources*& frame, u32& imageIndex, bool& recreateAfterPresent);
        bool submitPresentFrame(u32 width, u32 height, FrameResources& frame, u32 imageIndex, bool recreateAfterPresent);
        bool present(const TerminalUpdate& update);
        u32 materializeUpdates(FrameResources& frame, u64 appliedGeneration, bool initialized);
        void materializeCells(const TerminalCell* input, GpuCell* output, u16 count, u8 lineAttribute, const TerminalColors& colors);
        bool validateCachedCells(const TerminalCell* input, const GpuCell* output, u16 count, u8 lineAttribute);
        void ensureDamageJournal(u32 cellCount);
        void appendDamage(u32 begin, u32 count);
        void fullDamage();
        void collectDamage();
        void capturePresentationState(const TerminalUpdate& update);

        static bool needsFontGlyph(u32 id);
        static u32 packColor(const Color& color);
        static bool sameSelection(const Rect& lhs, const Rect& rhs);
    };

    // Thrown where VK_ERROR_SURFACE_LOST_KHR surfaces; caught only at the
    // update()/repaint() boundary, where the renderer dies with its pool.
    struct SurfaceLost {};

    [[noreturn]] void failVk(const char* operation, VkResult result) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string((int)(result)) + ")");
    }

    void checkVk(VkResult result, const char* operation) {
        if (result != VK_SUCCESS) {
            failVk(operation, result);
        }
    }

    VkImageSubresourceRange imageRange(u32 layers) {
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = layers;
        return range;
    }

    void imageBarrier(VkCommandBuffer commandBuffer, VkImage image, u32 layers, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = imageRange(layers);
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void bufferBarrier(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.size = size;
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    bool instanceHasExtension(const char* name) {
        u32 count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }

        Vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.mutData()) != VK_SUCCESS) {
            return false;
        }
        for (u32 index = 0; index < count; ++index) {
            if (StringView(extensions[index].extensionName) == StringView(name)) {
                return true;
            }
        }
        return false;
    }

    bool deviceHasExtension(VkPhysicalDevice physicalDevice, const char* name) {
        u32 count = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }

        Vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.mutData()) != VK_SUCCESS) {
            return false;
        }
        for (u32 index = 0; index < count; ++index) {
            if (StringView(extensions[index].extensionName) == StringView(name)) {
                return true;
            }
        }
        return false;
    }

    bool formatSupports(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatFeatureFlags features) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        return (properties.optimalTilingFeatures & features) == features;
    }

    bool deviceSupportsRenderer(VkPhysicalDevice physicalDevice) {
        return formatSupports(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && formatSupports(physicalDevice, VK_FORMAT_R8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    }

    VkCompositeAlphaFlagBitsKHR selectCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
        const VkCompositeAlphaFlagBitsKHR choices[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (const auto choice : choices) {
            if (supported & choice) {
                return choice;
            }
        }
        throw std::runtime_error("Vulkan surface has no composite alpha mode");
    }

}

RenderCache::RenderCache()
    : storage(renderCacheBytes)
{
    storage.setCapacity(renderCacheBytes);
    freeBlocks.grow(renderCacheBlockCount);
    u8* const memory = (u8*)(storage.mutData());
    for (size_t index = 0; index < renderCacheBlockCount; ++index) {
        freeBlocks.pushBack(new (memory + index * sizeof(RenderCacheBlock)) RenderCacheBlock);
    }
}

void RenderCache::render(RendererImpl& renderer, const TerminalCell* input, u16 count, u8 lineAttribute, GpuCell* output, const TerminalColors& colors, u64 context) {
    constexpr u16 minimumCachedCells = 8;

    if (count < minimumCachedCells) {
        renderer.materializeCells(input, output, count, lineAttribute, colors);
        return;
    }
    for (u16 offset = 0; offset < count;) {
        const u16 chunk = count - offset < renderCacheChunkCells ? count - offset : renderCacheChunkCells;
        u64 hash = shash64(input + offset, (size_t)(chunk) * sizeof(TerminalCell));
        hash ^= ((u64)(lineAttribute) + 1) * 0x9e3779b97f4a7c15ULL;
        hash ^= context;
        RenderCacheBlock*& bucket = buckets[hash & bucketMask];
        if (bucket != nullptr && bucket->hash == hash && renderer.validateCachedCells(input + offset, bucket->cells, chunk, lineAttribute)) {
            bucket->remove();
            lru.pushFront(bucket);
            memcpy(output + offset, bucket->cells, (size_t)(chunk) * sizeof(GpuCell));
            offset += chunk;
            continue;
        }
        if (bucket != nullptr) {
            bucket->remove();
            freeBlocks.pushBack(bucket);
            bucket = nullptr;
        }
        if (freeBlocks.empty()) {
            auto* const evicted = static_cast<RenderCacheBlock*>(lru.popBack());
            buckets[evicted->hash & bucketMask] = nullptr;
            freeBlocks.pushBack(evicted);
        }
        RenderCacheBlock* const block = freeBlocks.popBack();
        block->hash = hash;
        renderer.materializeCells(input + offset, block->cells, chunk, lineAttribute, colors);
        bucket = block;
        lru.pushFront(block);
        memcpy(output + offset, block->cells, (size_t)(chunk) * sizeof(GpuCell));
        offset += chunk;
    }
}

CallRendererFontChanged::CallRendererFontChanged(RendererImpl* renderer_)
    : renderer(renderer_)
{
}

void CallRendererFontChanged::onListen(void*) {
    renderer->resetFontResources();
}

CallRendererCellExtrasChanged::CallRendererCellExtrasChanged(RendererImpl* renderer_)
    : renderer(renderer_)
{
}

void CallRendererCellExtrasChanged::onListen(void*) {
    renderer->cellExtrasChanged();
}

RendererImpl::RendererImpl(Composer& composer_, const plt::RenderContext& context)
    : composer(composer_)
{
    chain = composer.smallObjects->make<SwapchainResources>();
    createInstance();
    createSurface(context);
    selectPhysicalDevice();
    createDevice();
    createCommandResources();
    createFontResources();
    createDescriptors();
    createPipelineLayout();
}

RendererImpl::~RendererImpl() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    destroySwapchainResources(*chain);
    composer.smallObjects->release(chain);
    collectRetiredSwapchains(true);
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    for (const VkPipeline retired : retiredPipelines) {
        vkDestroyPipeline(device, retired, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    if (atlasSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, atlasSampler, nullptr);
    }

    destroyFontResources(*fontResources);
    composer.smallObjects->release(fontResources);

    for (auto& frame : frames) {
        releaseBuffer(frame.cellBuffer, frame.cellMemory, frame.cells);
        releaseBuffer(frame.fontUploadBuffer, frame.fontUploadMemory, frame.fontUploads);
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        }
        if (frame.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, frame.fence, nullptr);
        }
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

void RendererImpl::createInstance() {
    const char* extensions[5] = {VK_KHR_SURFACE_EXTENSION_NAME};
    u32 extensionCount = 1;
    extensions[extensionCount++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    khrSurfaceMaintenance = instanceHasExtension(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    extSurfaceMaintenance = instanceHasExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    if (khrSurfaceMaintenance) {
        extensions[extensionCount++] = VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
    }
    if (extSurfaceMaintenance) {
        extensions[extensionCount++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "shitty";
    appInfo.applicationVersion = 0;
    appInfo.pEngineName = "shitty";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
}

void RendererImpl::createSurface(const plt::RenderContext& context) {
    if (context.backend != plt::RenderBackend::Wayland || context.connection == nullptr || context.window == nullptr) {
        throw std::runtime_error("Vulkan renderer requires a Wayland render context");
    }
    VkWaylandSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.display = (struct wl_display*)(context.connection);
    surfaceInfo.surface = (struct wl_surface*)(context.window);
    checkVk(vkCreateWaylandSurfaceKHR(instance, &surfaceInfo, nullptr, &surface), "vkCreateWaylandSurfaceKHR");
}

void RendererImpl::selectPhysicalDevice() {
    u32 deviceCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    VkPhysicalDeviceProperties bestProperties{};
    for (const auto candidate : devices) {
        if (!deviceHasExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !deviceSupportsRenderer(candidate)) {
            continue;
        }

        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        for (u32 family = 0; family < familyCount; ++family) {
            VkBool32 presentSupported = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentSupported), "vkGetPhysicalDeviceSurfaceSupportKHR");
            const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[family].queueFlags & required) != required || !presentSupported) {
                continue;
            }

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 50 : 10;
            if (score > bestScore) {
                bestScore = score;
                physicalDevice = candidate;
                queueFamily = family;
                bestProperties = properties;
            }
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "No Vulkan device supports compute rendering and window-system "
            "presentation"
        );
    }

    if (opts.vulkanInfo) {
        sysO << StringView(u8"Vulkan device: ") << StringView(bestProperties.deviceName) << StringView(u8"\nVulkan API: ") << (u64)(VK_VERSION_MAJOR(bestProperties.apiVersion)) << StringView(u8".") << (u64)(VK_VERSION_MINOR(bestProperties.apiVersion)) << StringView(u8".") << (u64)(VK_VERSION_PATCH(bestProperties.apiVersion)) << endL;
    }

    mutableSwapchainFormats = deviceHasExtension(physicalDevice, VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) && deviceHasExtension(physicalDevice, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    extendedStorageFormats = features.shaderStorageImageExtendedFormats;

    if (khrSurfaceMaintenance && deviceHasExtension(physicalDevice, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        swapchainMaintenanceExtension = VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    } else if (extSurfaceMaintenance && deviceHasExtension(physicalDevice, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        swapchainMaintenanceExtension = VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    }
    if (swapchainMaintenanceExtension != nullptr) {
        VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{};
        maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
        VkPhysicalDeviceFeatures2 available{};
        available.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        available.pNext = &maintenance;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &available);
        if (!maintenance.swapchainMaintenance1) {
            swapchainMaintenanceExtension = nullptr;
        }
    }
}

void RendererImpl::createDevice() {
    constexpr float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[5] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    u32 extensionCount = 1;
    if (mutableSwapchainFormats) {
        extensions[extensionCount++] = VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME;
        extensions[extensionCount++] = VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
    }
    constexpr const char* portabilitySubset = "VK_KHR_portability_subset";
    if (deviceHasExtension(physicalDevice, portabilitySubset)) {
        extensions[extensionCount++] = portabilitySubset;
    }
    if (swapchainMaintenanceExtension != nullptr) {
        extensions[extensionCount++] = swapchainMaintenanceExtension;
    }
    VkPhysicalDeviceFeatures features{};
    features.shaderStorageImageExtendedFormats = extendedStorageFormats;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{};
    if (swapchainMaintenanceExtension != nullptr) {
        maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
        maintenance.swapchainMaintenance1 = VK_TRUE;
    }
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = swapchainMaintenanceExtension == nullptr ? nullptr : &maintenance;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.pEnabledFeatures = &features;
    checkVk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
}

void RendererImpl::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    checkVk(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");

    std::array<VkCommandBuffer, framesInFlight> commandBuffers{};
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = framesInFlight;
    checkVk(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data()), "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (u32 i = 0; i < framesInFlight; ++i) {
        frames[i].commandBuffer = commandBuffers[i];
        checkVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].imageAvailable), "vkCreateSemaphore");
        checkVk(vkCreateFence(device, &fenceInfo, nullptr, &frames[i].fence), "vkCreateFence");
    }
}

u32 RendererImpl::findMemoryType(u32 allowed, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type found");
}

void RendererImpl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    checkVk(vkAllocateMemory(device, &allocationInfo, nullptr, &memory), "vkAllocateMemory");
    checkVk(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
}

RendererImpl::ImageResource RendererImpl::createImage(u32 width, u32 height, u32 layers, VkFormat format, VkImageUsageFlags usage, bool arrayView, VkFormat viewFormat) {
    if (viewFormat == VK_FORMAT_UNDEFINED) {
        viewFormat = format;
    }
    ImageResource result;
    result.width = width;
    result.height = height;
    result.layers = layers;

    try {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        if (viewFormat != format) {
            imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = layers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        checkVk(vkCreateImage(device, &imageInfo, nullptr, &result.image), "vkCreateImage");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, result.image, &requirements);
        VkMemoryAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkVk(vkAllocateMemory(device, &allocationInfo, nullptr, &result.memory), "vkAllocateMemory");
        checkVk(vkBindImageMemory(device, result.image, result.memory, 0), "vkBindImageMemory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.image;
        viewInfo.viewType = arrayView ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = viewFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = layers;
        checkVk(vkCreateImageView(device, &viewInfo, nullptr, &result.view), "vkCreateImageView");
    } catch (...) {
        destroyImage(result);
        throw;
    }
    return result;
}

VkImageView RendererImpl::createImageView(VkImage image, VkFormat format) const {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    checkVk(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView");
    return view;
}

void RendererImpl::destroyImage(ImageResource& image) {
    if (device != VK_NULL_HANDLE && image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image.view, nullptr);
    }
    if (device != VK_NULL_HANDLE && image.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image.image, nullptr);
    }
    if (device != VK_NULL_HANDLE && image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image.memory, nullptr);
    }
    image = {};
}

RendererImpl::GlyphCache::GlyphCache(ObjPool& pool)
    : refs(UnicodeMap<u16>::create(pool))
{
}

RendererImpl::FontResources::FontResources()
    : pool(ObjPool::fromMemory())
    , glyphs(*pool)
    , doubleWidthGlyphs(*pool)
{
}

void RendererImpl::configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget, u32 maxImageDimension) {
    constexpr u32 maximumSlots = 16384;
    const size_t glyphBytes = (size_t)(width)*composer.glyphHeight * layers;
    u32 requested = glyphBytes == 0 ? 2 : (u32)(byteBudget / glyphBytes);
    if (requested < 2) {
        requested = 2;
    }
    if (requested > maximumSlots) {
        requested = maximumSlots;
    }

    u32 maximumColumns = maxImageDimension / width;
    u32 maximumRows = maxImageDimension / composer.glyphHeight;
    if (maximumColumns > 256) {
        maximumColumns = 256;
    }
    if (maximumRows > 256) {
        maximumRows = 256;
    }
    if (maximumColumns == 0 || maximumRows == 0) {
        throw std::runtime_error("Font glyph does not fit a Vulkan image");
    }
    if (requested > maximumColumns * maximumRows) {
        requested = maximumColumns * maximumRows;
    }

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
        throw std::runtime_error("Could not size the Vulkan glyph cache");
    }

    cache.columns = bestColumns;
    cache.rows = bestRows;
    cache.slots.zero((size_t)(bestColumns)*bestRows);
}

RendererImpl::FontResources* RendererImpl::buildFontResources() {
    constexpr size_t atlasByteBudget = 16 * 1024 * 1024;
    constexpr size_t doubleWidthAtlasByteBudget = 8 * 1024 * 1024;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    FontResources* const resources = composer.smallObjects->make<FontResources>();
    try {
        configureGlyphCache(resources->glyphs, composer.glyphWidth, 4, atlasByteBudget, properties.limits.maxImageDimension2D);
        resources->atlas = createImage(composer.glyphWidth * resources->glyphs.columns, composer.glyphHeight * resources->glyphs.rows, 4, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);

        configureGlyphCache(resources->doubleWidthGlyphs, 2 * composer.glyphWidth, 1, doubleWidthAtlasByteBudget, properties.limits.maxImageDimension2D);
        resources->doubleWidthAtlas = createImage(2 * composer.glyphWidth * resources->doubleWidthGlyphs.columns, composer.glyphHeight * resources->doubleWidthGlyphs.rows, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);

        if (atlasSampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 0.0f;
            checkVk(vkCreateSampler(device, &samplerInfo, nullptr, &atlasSampler), "vkCreateSampler");
        }
    } catch (...) {
        destroyFontResources(*resources);
        composer.smallObjects->release(resources);
        throw;
    }
    return resources;
}

void RendererImpl::destroyFontResources(FontResources& resources) {
    destroyImage(resources.doubleWidthColorAtlas);
    destroyImage(resources.doubleWidthAtlas);
    destroyImage(resources.colorAtlas);
    destroyImage(resources.atlas);
}

void RendererImpl::createFontResources() {
    fontResources = buildFontResources();
}

void RendererImpl::resetFontResources() {
    FontResources* const replacement = buildFontResources();
    try {
        checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
    } catch (...) {
        destroyFontResources(*replacement);
        composer.smallObjects->release(replacement);
        throw;
    }

    FontResources* const previous = fontResources;
    fontResources = replacement;
    updateStaticDescriptors();
    destroyFontResources(*previous);
    composer.smallObjects->release(previous);
    fontUploadData.reset();
    atlasCopies.clear();
    colorAtlasCopies.clear();
    doubleWidthAtlasCopies.clear();
    doubleWidthColorAtlasCopies.clear();
    atlasInitialized = false;
    colorAtlasInitialized = false;
    doubleWidthAtlasInitialized = false;
    doubleWidthColorAtlasInitialized = false;
    previousStateValid = false;
}

void RendererImpl::cellExtrasChanged() {
    fontResources->glyphs.graphemeRefs.zero(fontResources->glyphs.graphemeRefs.used());
    fontResources->doubleWidthGlyphs.graphemeRefs.zero(fontResources->doubleWidthGlyphs.graphemeRefs.used());
}

void RendererImpl::createDescriptors() {
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (u32 binding = 2; binding < 6; ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    checkVk(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout), "vkCreateDescriptorSetLayout");

    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, framesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * framesInFlight},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = framesInFlight;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    checkVk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    std::array<VkDescriptorSetLayout, framesInFlight> layouts;
    layouts.fill(descriptorSetLayout);
    std::array<VkDescriptorSet, framesInFlight> sets{};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = framesInFlight;
    allocateInfo.pSetLayouts = layouts.data();
    checkVk(vkAllocateDescriptorSets(device, &allocateInfo, sets.data()), "vkAllocateDescriptorSets");
    for (u32 i = 0; i < framesInFlight; ++i) {
        frames[i].descriptorSet = sets[i];
    }
    updateStaticDescriptors();
}

void RendererImpl::updateStaticDescriptors() {
    const ImageResource& wideAtlas = fontResources->doubleWidthAtlas.image != VK_NULL_HANDLE ? fontResources->doubleWidthAtlas : fontResources->atlas;
    const ImageResource& primaryColor = fontResources->colorAtlas.image != VK_NULL_HANDLE ? fontResources->colorAtlas : fontResources->atlas;
    const ImageResource& wideColor = fontResources->doubleWidthColorAtlas.image != VK_NULL_HANDLE ? fontResources->doubleWidthColorAtlas : wideAtlas;

    for (auto& frame : frames) {
        const VkDescriptorImageInfo imageInfos[] = {
            {atlasSampler, fontResources->atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, primaryColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (u32 i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.descriptorSet;
            writes[i].dstBinding = i + 2;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    }
}

void RendererImpl::updateOutputDescriptor(FrameResources& frame, VkImageView view) {
    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frame.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void RendererImpl::updateCellDescriptor(FrameResources& frame) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = frame.cellBuffer;
    bufferInfo.range = frame.cellCapacity;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frame.descriptorSet;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void RendererImpl::createPipelineLayout() {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    checkVk(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
}

void RendererImpl::selectPipeline(const GeneratedRenderShader& shader) {
    if (activeShader == &shader) {
        return;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = shader.codeSize;
    moduleInfo.pCode = shader.code;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule), "vkCreateShaderModule");

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;
    VkPipeline replacement = VK_NULL_HANDLE;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &replacement);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    checkVk(result, "vkCreateComputePipelines");
    if (pipeline != VK_NULL_HANDLE) {
        retiredPipelines.pushBack(pipeline);
    }
    pipeline = replacement;
    activeShader = &shader;
}

void RendererImpl::destroySwapchainResources(SwapchainResources& resources) {
    for (const auto view : resources.views) {
        if (device != VK_NULL_HANDLE && view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    for (const auto semaphore : resources.semaphores) {
        if (device != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    for (const VkFence fence : resources.presentFences) {
        if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
    }
    resources.views.clear();
    resources.semaphores.clear();
    resources.presentFences.clear();
    resources.presentFencePending.clear();
    resources.images.clear();
    resources.initialized.clear();
    resources.generations.clear();
    if (resources.swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, resources.swapchain, nullptr);
    }
    resources.swapchain = VK_NULL_HANDLE;
    resources.format = VK_FORMAT_UNDEFINED;
    resources.storageViewFormat = VK_FORMAT_UNDEFINED;
    resources.extent = {};
    destroyImage(resources.output);
    resources.outputInitialized = false;
    resources.outputGeneration = 0;
    resources.direct = false;
}

void RendererImpl::retireSwapchain(SwapchainResources* resources) {
    if (resources->swapchain == VK_NULL_HANDLE && resources->output.image == VK_NULL_HANDLE) {
        destroySwapchainResources(*resources);
        composer.smallObjects->release(resources);
        return;
    }
    if (swapchainMaintenanceExtension == nullptr) {
        resources->gracePresents = framesInFlight + 1;
    }
    retiredSwapchains.pushBack(resources);
    collectRetiredSwapchains();
    if (swapchainMaintenanceExtension == nullptr && retiredSwapchains.length() >= 8) {
        // No frame was presented since several retirements (a resize storm
        // with failing presents): fall back to a hard sync.
        checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        collectRetiredSwapchains(true);
    }
}

void RendererImpl::collectRetiredSwapchains(bool force) {
    for (size_t index = 0; index != retiredSwapchains.length();) {
        SwapchainResources* const resources = retiredSwapchains[index];
        bool ready = force;
        if (!ready && swapchainMaintenanceExtension == nullptr) {
            // Own queue work is fenced framesInFlight frames later; the
            // extra frame is margin for the presentation engine, which is
            // unobservable without the maintenance extension.
            ready = resources->gracePresents == 0;
        }
        if (!ready && swapchainMaintenanceExtension != nullptr) {
            ready = true;
            for (size_t fenceIndex = 0; fenceIndex != resources->presentFences.length(); ++fenceIndex) {
                if (!resources->presentFencePending[fenceIndex]) {
                    continue;
                }
                const VkResult status = vkGetFenceStatus(device, resources->presentFences[fenceIndex]);
                if (status == VK_NOT_READY) {
                    ready = false;
                    break;
                }
                checkVk(status, "vkGetFenceStatus");
            }
        }
        if (!ready) {
            ++index;
            continue;
        }
        destroySwapchainResources(*resources);
        composer.smallObjects->release(resources);
        retiredSwapchains.mut(index) = retiredSwapchains.back();
        retiredSwapchains.popBack();
    }
}

RendererImpl::PresentTarget RendererImpl::selectPresentTarget(const VkSurfaceCapabilitiesKHR& capabilities, const std::vector<VkSurfaceFormatKHR>& formats) const {
    PresentTarget target;
    const VkImageUsageFlags directUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((capabilities.supportedUsageFlags & directUsage) == directUsage) {
        for (const GeneratedRenderShader& candidate : generatedRenderShaders) {
            if ((candidate.flags & renderShaderMutableFormat) && !mutableSwapchainFormats) {
                continue;
            }
            if ((candidate.flags & renderShaderExtendedStorage) && !extendedStorageFormats) {
                continue;
            }
            if (!formatSupports(physicalDevice, candidate.storageViewFormat, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
                continue;
            }
            for (const VkSurfaceFormatKHR& format : formats) {
                if (format.format == candidate.presentFormat && format.colorSpace == candidate.colorSpace) {
                    target.format = format;
                    target.shader = &candidate;
                    break;
                }
            }
            if (target.shader != nullptr) {
                break;
            }
        }
    }

    target.direct = target.shader != nullptr;
    if (!target.direct) {
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            throw std::runtime_error("Vulkan surface supports neither storage output nor transfer destination");
        }
        const VkFormat preferredFormats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB,
        };
        for (const auto preferred : preferredFormats) {
            const auto found = std::find_if(formats.begin(), formats.end(), [this, preferred](const VkSurfaceFormatKHR& format) {
                // The shader produces sRGB-encoded bytes; other color
                // spaces would need a conversion the blit path lacks.
                return format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && formatSupports(physicalDevice, format.format, VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
            });
            if (found != formats.end()) {
                target.format = *found;
                break;
            }
        }
        if (target.format.format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("Vulkan surface has no usable direct or blit format");
        }
        target.shader = &fallbackRenderShader;
    }

    if (target.direct && target.format.format != target.shader->storageViewFormat && !(target.shader->flags & renderShaderMutableFormat)) {
        throw std::runtime_error("Generated render shader requires an undeclared mutable format");
    }
    return target;
}

void RendererImpl::createSwapchain(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    const VkResult capabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    if (capabilitiesResult == VK_ERROR_SURFACE_LOST_KHR) {
        throw SurfaceLost{};
    }
    checkVk(capabilitiesResult, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    u32 formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formatCount == 0) {
        throw std::runtime_error("Vulkan surface exposes no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");

    const PresentTarget target = selectPresentTarget(capabilities, formats);
    const VkSurfaceFormatKHR surfaceFormat = target.format;
    const GeneratedRenderShader* const renderShader = target.shader;
    const bool direct = target.direct;

    u32 presentModeCount = 0;
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    VkExtent2D extent{};
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    u32 imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = direct ? (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = selectCompositeAlpha(capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = chain->swapchain;

    VkFormat viewFormats[2] = {
        surfaceFormat.format,
        renderShader->storageViewFormat,
    };
    VkImageFormatListCreateInfo formatList{};
    const bool mutableFormat = direct && (renderShader->flags & renderShaderMutableFormat);
    if (mutableFormat) {
        formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        formatList.viewFormatCount = viewFormats[0] == viewFormats[1] ? 1u : 2u;
        formatList.pViewFormats = viewFormats;
        createInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        createInfo.pNext = &formatList;
    }

    SwapchainResources* const replacement = composer.smallObjects->make<SwapchainResources>();
    replacement->format = surfaceFormat.format;
    replacement->storageViewFormat = renderShader->storageViewFormat;
    replacement->extent = extent;
    replacement->direct = direct;
    try {
        if (!direct) {
            // The shader stores already-sRGB-encoded bytes through a raw
            // UNORM view. Blitting a UNORM image into an sRGB swapchain
            // image would encode them a second time; giving the output
            // image an sRGB format makes the blit's decode+encode cancel
            // (and handles the channel order of BGRA targets).
            const bool srgbTarget = surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB || surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB;
            const VkFormat outputFormat = srgbTarget ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            replacement->output = createImage(width, height, 1, outputFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, VK_FORMAT_R8G8B8A8_UNORM);
        }
        checkVk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &replacement->swapchain), "vkCreateSwapchainKHR");
        checkVk(vkGetSwapchainImagesKHR(device, replacement->swapchain, &imageCount, nullptr), "vkGetSwapchainImagesKHR");
        replacement->images.zero(imageCount);
        checkVk(vkGetSwapchainImagesKHR(device, replacement->swapchain, &imageCount, replacement->images.mutData()), "vkGetSwapchainImagesKHR");
        if (direct) {
            replacement->views.zero(imageCount);
            for (u32 index = 0; index < imageCount; ++index) {
                replacement->views.mut(index) = createImageView(replacement->images[index], replacement->storageViewFormat);
            }
        }
        replacement->semaphores.zero(imageCount);
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (VkSemaphore* semaphore = replacement->semaphores.mutBegin(); semaphore != replacement->semaphores.mutEnd(); ++semaphore) {
            checkVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, semaphore), "vkCreateSemaphore");
        }
        if (swapchainMaintenanceExtension != nullptr) {
            replacement->presentFences.zero(imageCount);
            replacement->presentFencePending.zero(imageCount);
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            for (VkFence* fence = replacement->presentFences.mutBegin(); fence != replacement->presentFences.mutEnd(); ++fence) {
                checkVk(vkCreateFence(device, &fenceInfo, nullptr, fence), "vkCreateFence");
            }
        }
        replacement->initialized.zero(imageCount);
        replacement->generations.zero(imageCount);
        selectPipeline(*renderShader);
    } catch (...) {
        destroySwapchainResources(*replacement);
        composer.smallObjects->release(replacement);
        throw;
    }

    SwapchainResources* const previous = chain;
    chain = replacement;
    renderExtent = {width, height};
    if (opts.vulkanInfo) {
        sysO << StringView(u8"Vulkan presentation: ") << StringView(direct ? u8"direct storage (" : u8"offscreen blit (") << StringView(renderShader->name) << StringView(u8")") << endL;
    }
    retireSwapchain(previous);
}

bool RendererImpl::tryCreateSwapchain(u32 width, u32 height) {
    try {
        createSwapchain(width, height);
        return true;
    } catch (const SurfaceLost&) {
        throw;
    } catch (...) {
        return false;
    }
}

void RendererImpl::releaseBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped) {
    if (mapped != nullptr) {
        vkUnmapMemory(device, memory);
        mapped = nullptr;
    }
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void RendererImpl::ensureCellBuffer(FrameResources& frame, size_t bytes) {
    if (frame.cellCapacity >= bytes) {
        return;
    }

    releaseBuffer(frame.cellBuffer, frame.cellMemory, frame.cells);
    createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.cellBuffer, frame.cellMemory);
    checkVk(vkMapMemory(device, frame.cellMemory, 0, bytes, 0, &frame.cells), "vkMapMemory");
    frame.cellCapacity = bytes;
    updateCellDescriptor(frame);
}

void RendererImpl::ensureFontUploadBuffer(FrameResources& frame, size_t bytes) {
    if (frame.fontUploadCapacity >= bytes) {
        return;
    }

    releaseBuffer(frame.fontUploadBuffer, frame.fontUploadMemory, frame.fontUploads);
    createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.fontUploadBuffer, frame.fontUploadMemory);
    checkVk(vkMapMemory(device, frame.fontUploadMemory, 0, bytes, 0, &frame.fontUploads), "vkMapMemory");
    frame.fontUploadCapacity = bytes;
}

void RendererImpl::ensureColorAtlas(bool doubleWidth) {
    ImageResource& image = doubleWidth ? fontResources->doubleWidthColorAtlas : fontResources->colorAtlas;
    if (image.image != VK_NULL_HANDLE) {
        return;
    }

    checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
    const GlyphCache& cache = doubleWidth ? fontResources->doubleWidthGlyphs : fontResources->glyphs;
    const u32 width = composer.glyphWidth * (doubleWidth ? 2 : 1);
    const u32 layers = doubleWidth ? 1 : 4;
    image = createImage(width * cache.columns, composer.glyphHeight * cache.rows, layers, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);
    updateStaticDescriptors();
}

bool RendererImpl::needsFontGlyph(u32 id) {
    return id != 0x200d && !(id >= 0xfe00 && id <= 0xfe0f) && !(id >= 0xe0100 && id <= 0xe01ef) && !(id >= 0x2500 && id <= 0x257f) && !(id >= 0x23ba && id <= 0x23bd);
}

void RendererImpl::beginGlyphFrame() {
    fontUploadData.reset();
    atlasCopies.clear();
    colorAtlasCopies.clear();
    doubleWidthAtlasCopies.clear();
    doubleWidthColorAtlasCopies.clear();

    auto advance = [](GlyphCache& cache) {
        ++cache.generation;
        if (cache.generation == 0) {
            for (GlyphSlot* slot = cache.slots.mutBegin(); slot != cache.slots.mutEnd(); ++slot) {
                slot->generation = 0;
            }
            cache.generation = 1;
        }
    };
    advance(fontResources->glyphs);
    if (fontResources->doubleWidthAtlas.image != VK_NULL_HANDLE) {
        advance(fontResources->doubleWidthGlyphs);
    }
}

void RendererImpl::pinVisibleGlyphs() {
    for (GpuCell* cell = cells.mutBegin(); cell != cells.mutEnd(); ++cell) {
        if (cell->glyph == 0) {
            continue;
        }
        GlyphCache& cache = cell->lineAttribute != 0 || (cell->attributes & gpuDoubleWidth) != 0 ? fontResources->doubleWidthGlyphs : fontResources->glyphs;
        const u32 slot = (cell->glyph & 0xff) + (((cell->glyph >> 8) & 0xff) * cache.columns);
        if (slot < cache.slots.length()) {
            cache.slots.mut(slot).generation = cache.generation;
        }
    }
}

u16 RendererImpl::allocateGlyphSlot(GlyphCache& cache, u32 id, bool grapheme) {
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
            const size_t oldOffset = (size_t)(state.id) * sizeof(u16);
            if (oldOffset + sizeof(u16) <= cache.graphemeRefs.used()) {
                u16* oldRef = (u16*)((u8*)(cache.graphemeRefs.mutData()) + oldOffset);
                if (*oldRef == slot) {
                    *oldRef = 0;
                }
            }
        } else if (u16* oldRef = cache.refs->find(state.id); oldRef != nullptr && *oldRef == slot) {
            *oldRef = 0;
        }
        state.id = id;
        state.generation = cache.generation;
        state.layers = 0;
        state.colorLayers = 0;
        state.grapheme = grapheme;
        return slot;
    }
    return 0;
}

VkDeviceSize RendererImpl::stageFontData(const void* data, size_t len, size_t expected) {
    const u32 zero = 0;
    const size_t padding = (4 - (fontUploadData.used() & 3)) & 3;
    fontUploadData.append(&zero, padding);
    const VkDeviceSize offset = fontUploadData.used();
    if (len == expected) {
        fontUploadData.append(data, len);
    } else {
        fontUploadData.growDelta(expected);
        void* begin = fontUploadData.mutCurrent();
        fontUploadData.seekRelative(expected);
        memZero(begin, fontUploadData.mutCurrent());
    }
    return offset;
}

u32 RendererImpl::ensureGlyph(Fontpack& fonts, const u32* codepoints, size_t count, u32 id, bool grapheme, FontStyle style, bool doubleWidth) {
    if (count == 0 || (!grapheme && (id >= 0x110000 || !needsFontGlyph(id)))) {
        return 0;
    }

    GlyphCache& cache = doubleWidth ? fontResources->doubleWidthGlyphs : fontResources->glyphs;
    u16* ref = nullptr;
    if (grapheme) {
        const size_t required = ((size_t)(id) + 1) * sizeof(u16);
        if (required > cache.graphemeRefs.used()) {
            const size_t previous = cache.graphemeRefs.used();
            cache.graphemeRefs.grow(required);
            cache.graphemeRefs.seekAbsolute(required);
            memZero((u8*)(cache.graphemeRefs.mutData()) + previous, cache.graphemeRefs.mutCurrent());
        }
        ref = (u16*)((u8*)(cache.graphemeRefs.mutData()) + (size_t)(id) * sizeof(u16));
    } else {
        ref = &(*cache.refs)[id];
    }
    u16 slot = *ref;
    if (slot != 0) {
        const GlyphSlot& state = cache.slots[slot];
        if (state.id != id || state.grapheme != grapheme) {
            slot = 0;
            *ref = 0;
        }
    }
    if (slot == 0) {
        slot = allocateGlyphSlot(cache, id, grapheme);
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
        const bool color = state.colorLayers & layerMask;
        return (slot % cache.columns) | ((slot / cache.columns) << 8) | ((u32)(color) << 16);
    }

    const u32 width = doubleWidth ? 2 * composer.glyphWidth : composer.glyphWidth;
    const FontGlyph glyph = fonts.glyph(codepoints, count, style, doubleWidth);
    const size_t bytes = (size_t)(width)*composer.glyphHeight * (glyph.color ? 4 : 1);
    if (glyph.color) {
        ensureColorAtlas(doubleWidth);
    }
    VkBufferImageCopy copy{};
    copy.bufferOffset = stageFontData(glyph.data, glyph.len, bytes);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.baseArrayLayer = layer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {
        (i32)((slot % cache.columns) * width),
        (i32)((slot / cache.columns) * composer.glyphHeight),
        0,
    };
    copy.imageExtent = {width, composer.glyphHeight, 1};
    if (doubleWidth) {
        (glyph.color ? doubleWidthColorAtlasCopies : doubleWidthAtlasCopies).pushBack(copy);
    } else {
        (glyph.color ? colorAtlasCopies : atlasCopies).pushBack(copy);
    }
    state.layers |= layerMask;
    if (glyph.color) {
        state.colorLayers |= layerMask;
    }
    return (slot % cache.columns) | ((slot / cache.columns) << 8) | ((u32)(glyph.color) << 16);
}

void RendererImpl::materializeCells(const TerminalCell* input, GpuCell* output, u16 count, u8 lineAttribute, const TerminalColors& colors) {
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
            if (graphemeId != 0) {
                glyph = ensureGlyph(fonts, grapheme.data(), grapheme.size(), graphemeId, true, style, doubleWidth);
            } else {
                glyph = ensureGlyph(fonts, &codepoint, 1, codepoint, false, style, doubleWidth);
            }
        }
        output[index] = {
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

bool RendererImpl::validateCachedCells(const TerminalCell* input, const GpuCell* output, u16 count, u8 lineAttribute) {
    CellExtraStore& extras = *composer.cellExtras;
    for (u16 index = 0; index < count; ++index) {
        const TerminalCell& cell = input[index];
        const GpuCell& rendered = output[index];
        const bool doubleWidth = lineAttribute != 0 || cell.dwidth;
        if (cell.dwidth_cont && lineAttribute == 0) {
            if (rendered.glyph != 0) {
                return false;
            }
            continue;
        }
        u32 id = cell.uc_pt ? cell.uc_pt : ' ';
        bool grapheme = false;
        if (cell.hasExtra()) {
            const CellExtraView extra = extras.view(cell);
            if (!extra.grapheme.empty()) {
                id = cell.extraRef();
                grapheme = true;
            }
        }
        if (!grapheme && !needsFontGlyph(id)) {
            if (rendered.glyph != 0) {
                return false;
            }
            continue;
        }
        if (rendered.glyph == 0) {
            return false;
        }
        GlyphCache& cache = doubleWidth ? fontResources->doubleWidthGlyphs : fontResources->glyphs;
        const u32 slot = (rendered.glyph & 0xff) + (((rendered.glyph >> 8) & 0xff) * cache.columns);
        if (slot >= cache.slots.length()) {
            return false;
        }
        GlyphSlot& state = cache.slots.mut(slot);
        const u32 layer = doubleWidth ? 0 : ((cell.bold ? 1u : 0u) | (cell.italic ? 2u : 0u));
        const u8 layerMask = (u8)(1u << layer);
        if (state.id != id || state.grapheme != grapheme || (state.layers & layerMask) == 0 || ((state.colorLayers & layerMask) != 0) != ((rendered.glyph & (1u << 16)) != 0)) {
            return false;
        }
        state.generation = cache.generation;
    }
    return true;
}

void RendererImpl::recordImageUploads(VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, const ImageResource& image, const Vector<VkBufferImageCopy>& copies, bool initialize) {
    if (!initialize && copies.empty()) {
        return;
    }

    imageBarrier(commandBuffer, image.image, image.layers, initialize ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, initialize ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, initialize ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (initialize) {
        const VkClearColorValue clear{};
        const VkImageSubresourceRange range = imageRange(image.layers);
        vkCmdClearColorImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    }
    if (!copies.empty()) {
        if (initialize) {
            imageBarrier(commandBuffer, image.image, image.layers, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copies.length(), copies.data());
    }

    imageBarrier(commandBuffer, image.image, image.layers, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void RendererImpl::recordFontUploads(FrameResources& frame) {
    const bool initialize = !atlasInitialized || (fontResources->doubleWidthAtlas.image != VK_NULL_HANDLE && !doubleWidthAtlasInitialized) || (fontResources->colorAtlas.image != VK_NULL_HANDLE && !colorAtlasInitialized) || (fontResources->doubleWidthColorAtlas.image != VK_NULL_HANDLE && !doubleWidthColorAtlasInitialized);
    if (!initialize && fontUploadData.empty()) {
        return;
    }

    if (!fontUploadData.empty()) {
        bufferBarrier(frame.commandBuffer, frame.fontUploadBuffer, fontUploadData.used(), VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, fontResources->atlas, atlasCopies, !atlasInitialized);
    atlasInitialized = true;
    if (fontResources->colorAtlas.image != VK_NULL_HANDLE) {
        recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, fontResources->colorAtlas, colorAtlasCopies, !colorAtlasInitialized);
        colorAtlasInitialized = true;
    }
    if (fontResources->doubleWidthAtlas.image != VK_NULL_HANDLE) {
        recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, fontResources->doubleWidthAtlas, doubleWidthAtlasCopies, !doubleWidthAtlasInitialized);
        doubleWidthAtlasInitialized = true;
    }
    if (fontResources->doubleWidthColorAtlas.image != VK_NULL_HANDLE) {
        recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, fontResources->doubleWidthColorAtlas, doubleWidthColorAtlasCopies, !doubleWidthColorAtlasInitialized);
        doubleWidthColorAtlasInitialized = true;
    }
}

u32 RendererImpl::packColor(const Color& color) {
    return (u32)(color.red) | ((u32)(color.green) << 8) | ((u32)(color.blue) << 16);
}

bool RendererImpl::sameSelection(const Rect& lhs, const Rect& rhs) {
    return lhs.tl == rhs.tl && lhs.br == rhs.br && lhs.rectangular == rhs.rectangular;
}

void RendererImpl::ensureDamageJournal(u32 cellCount) {
    if (damage.capacity >= cellCount) {
        return;
    }
    STD_ASSERT(damage.count == 0);
    damageJournalStorage.grow((size_t)(cellCount) * sizeof(RenderDamage::Entry));
    damage.configure((RenderDamage::Entry*)(damageJournalStorage.mutData()), cellCount);
}

void RendererImpl::fullDamage() {
    damage.full();
}

void RendererImpl::appendDamage(u32 begin, u32 count) {
    STD_ASSERT((size_t)(begin) + count <= cells.length());
    damage.add(begin, count);
}

void RendererImpl::collectDamage() {
    u64 applied = damage.generation;
    if (chain->direct) {
        for (u32 index = 0; index < chain->initialized.length(); ++index) {
            if (chain->initialized[index] && chain->generations[index] < applied) {
                applied = chain->generations[index];
            }
        }
    } else if (chain->outputInitialized) {
        applied = chain->outputGeneration;
    }
    damage.collect(applied);
}

void RendererImpl::capturePresentationState(const TerminalUpdate& update) {
    presentationState.cursor = update.cursor;
    presentationState.selection = update.snappedSelection;
    presentationState.selectionForeground = update.selectionForeground;
    presentationState.selectionBackground = update.selectionBackground;
    presentationState.selectionColorMask = update.selectionColorMask;
    presentationState.hoveredHyperlink = update.hoveredHyperlink;
    presentationState.hoveredLinkBegin = update.hoveredLinkBegin;
    presentationState.hoveredLinkEnd = update.hoveredLinkEnd;
    presentationState.screenReverse = update.screenReverse;
    presentationState.blinkVisible = update.blinkVisible;
    presentationState.cursorBlink = update.cursorBlink;
}

u32 RendererImpl::materializeUpdates(FrameResources& frame, u64 appliedGeneration, bool initialized) {
    const size_t cellCount = cells.length();
    ensureCellBuffer(frame, cellCount * sizeof(GpuCellUpdate));
    auto* const gpuUpdates = (GpuCellUpdate*)(frame.cells);

    if (updateEpochs.used() < cellCount * sizeof(u32)) {
        updateEpochs.zero(cellCount * sizeof(u32));
    }
    if (++updateEpoch == 0) {
        updateEpochs.zero(updateEpochs.used());
        updateEpoch = 1;
    }
    auto* const epochs = (u32*)(updateEpochs.mutData());
    u32 gpuUpdateCount = 0;

    const auto appendSource = [&](u32 sourceIndex) {
        STD_ASSERT(sourceIndex < cellCount);
        const u32 sourceRow = sourceIndex / cellColumns;
        const u32 sourceColumn = sourceIndex - sourceRow * cellColumns;
        const u32 rowIndex = sourceRow * cellColumns;
        const u8 lineAttribute = (u8)(cells[rowIndex].lineAttribute);

        u32 outputIndices[2];
        u32 outputCount = 1;
        if (lineAttribute == 0) {
            outputIndices[0] = sourceIndex;
        } else {
            const u32 firstColumn = sourceColumn * 2;
            if (firstColumn >= cellColumns) {
                return;
            }
            outputIndices[0] = rowIndex + firstColumn;
            if (firstColumn + 1 < cellColumns) {
                outputIndices[1] = outputIndices[0] + 1;
                outputCount = 2;
            }
        }

        bool needed[2]{};
        bool anyNeeded = false;
        for (u32 index = 0; index < outputCount; ++index) {
            const u32 outputIndex = outputIndices[index];
            if (epochs[outputIndex] == updateEpoch) {
                continue;
            }
            epochs[outputIndex] = updateEpoch;
            needed[index] = true;
            anyNeeded = true;
        }
        if (!anyNeeded) {
            return;
        }

        GpuCell cell = cells[sourceIndex];
        if (lineAttribute == 0 && (cell.attributes & gpuDoubleWidth) != 0 && (sourceColumn + 1 >= cellColumns || (cells[sourceIndex + 1].attributes & gpuDoubleWidthContinuation) == 0)) {
            cell.attributes &= ~gpuDoubleWidth;
        }
        for (u32 index = 0; index < outputCount; ++index) {
            if (!needed[index]) {
                continue;
            }
            STD_ASSERT(gpuUpdateCount < cellCount);
            gpuUpdates[gpuUpdateCount++] = {
                sourceIndex,
                outputIndices[index],
                cell,
            };
        }
    };

    if (damage.requiresFull(appliedGeneration, initialized)) {
        for (u32 index = 0; index < cellCount; ++index) {
            appendSource(index);
        }
    } else {
        for (u32 entryIndex = 0; entryIndex < damage.count; ++entryIndex) {
            const RenderDamage::Entry& entry = damage.entry(entryIndex);
            if (entry.generation <= appliedGeneration) {
                continue;
            }
            for (u32 index = 0; index < entry.count; ++index) {
                appendSource(entry.begin + index);
            }
        }
    }

    if (!fontUploadData.empty()) {
        ensureFontUploadBuffer(frame, fontUploadData.used());
        __builtin_memcpy(frame.fontUploads, fontUploadData.data(), fontUploadData.used());
    }
    return gpuUpdateCount;
}

void RendererImpl::recordCommands(FrameResources& frame, u32 imageIndex, const PresentationState& state, u32 updateCount, bool clearOutput) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    recordFontUploads(frame);

    const VkImage output = chain->direct ? chain->images[imageIndex] : chain->output.image;
    const VkImageView outputView = chain->direct ? chain->views[imageIndex] : chain->output.view;
    const bool initialized = chain->direct ? chain->initialized[imageIndex] : chain->outputInitialized;
    updateOutputDescriptor(frame, outputView);

    // Between frames the blit-path output image rests in GENERAL, a
    // direct-path swapchain image in PRESENT_SRC.
    const VkImageLayout restingLayout = chain->direct ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_GENERAL;
    const VkAccessFlags restingAccess = chain->direct ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT;
    if (clearOutput) {
        imageBarrier(frame.commandBuffer, output, 1, initialized ? restingAccess : 0, VK_ACCESS_TRANSFER_WRITE_BIT, initialized ? restingLayout : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, initialized ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkClearColorValue clearColor{{
            clearBackground.red / 255.0f,
            clearBackground.green / 255.0f,
            clearBackground.blue / 255.0f,
            1.0f,
        }};
        const VkImageSubresourceRange outputRange = imageRange(1);
        vkCmdClearColorImage(frame.commandBuffer, output, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &outputRange);

        imageBarrier(frame.commandBuffer, output, 1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    } else {
        imageBarrier(frame.commandBuffer, output, 1, restingAccess, VK_ACCESS_SHADER_WRITE_BIT, restingLayout, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    if (updateCount != 0) {
        bufferBarrier(frame.commandBuffer, frame.cellBuffer, (size_t)(updateCount) * sizeof(GpuCellUpdate), VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        const PushConstants pushConstants{
            composer.glyphWidth,
            composer.glyphHeight,
            composer.columns,
            composer.rows,
            chain->direct ? chain->extent.width : composer.pixelWidth,
            chain->direct ? chain->extent.height : composer.pixelHeight,
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
            fontResources->doubleWidthAtlas.image != VK_NULL_HANDLE ? 1u : 0u,
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
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
        vkCmdDispatch(frame.commandBuffer, (updateCount + 63) / 64, 1, 1);
    }

    if (chain->direct) {
        imageBarrier(frame.commandBuffer, output, 1, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
        return;
    }

    recordBlit(frame, imageIndex, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
}

void RendererImpl::recordBlit(FrameResources& frame, u32 imageIndex, VkAccessFlags outputSrcAccess, VkPipelineStageFlags outputSrcStage) {
    imageBarrier(frame.commandBuffer, chain->output.image, 1, outputSrcAccess, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, outputSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const bool initialized = chain->initialized[imageIndex];
    imageBarrier(frame.commandBuffer, chain->images[imageIndex], 1, initialized ? VK_ACCESS_MEMORY_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT, initialized ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, initialized ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {(i32)(renderExtent.width), (i32)(renderExtent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {(i32)(chain->extent.width), (i32)(chain->extent.height), 1};
    vkCmdBlitImage(frame.commandBuffer, chain->output.image, VK_IMAGE_LAYOUT_GENERAL, chain->images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    imageBarrier(frame.commandBuffer, chain->images[imageIndex], 1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void RendererImpl::recordRepaintCommands(FrameResources& frame, u32 imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    recordBlit(frame, imageIndex, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
}

bool RendererImpl::acquirePresentFrame(u32 width, u32 height, FrameResources*& frame, u32& imageIndex, bool& recreateAfterPresent) {
    frame = &frames[currentFrame];
    checkVk(vkWaitForFences(device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    VkResult result = vkAcquireNextImageKHR(device, chain->swapchain, UINT64_MAX, frame->imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        throw SurfaceLost{};
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        tryCreateSwapchain(width, height);
        return false;
    }
    recreateAfterPresent = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        failVk("vkAcquireNextImageKHR", result);
    }
    checkVk(vkResetCommandBuffer(frame->commandBuffer, 0), "vkResetCommandBuffer");
    return true;
}

bool RendererImpl::submitPresentFrame(u32 width, u32 height, FrameResources& frame, u32 imageIndex, bool recreateAfterPresent) {
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &chain->semaphores[imageIndex];
    // Reset only when committed to submitting: a throw between an early
    // reset and the submit would leave the fence unsignaled forever, and
    // the next wait on it would hang.
    checkVk(vkResetFences(device, 1, &frame.fence), "vkResetFences");
    checkVk(vkQueueSubmit(queue, 1, &submitInfo, frame.fence), "vkQueueSubmit");
    chain->initialized.mut(imageIndex) = true;

    VkSwapchainPresentFenceInfoKHR presentFenceInfo{};
    VkFence presentFence = VK_NULL_HANDLE;
    if (swapchainMaintenanceExtension != nullptr) {
        presentFence = chain->presentFences[imageIndex];
        if (chain->presentFencePending[imageIndex]) {
            checkVk(vkWaitForFences(device, 1, &presentFence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            checkVk(vkResetFences(device, 1, &presentFence), "vkResetFences");
        }
        presentFenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
        presentFenceInfo.swapchainCount = 1;
        presentFenceInfo.pFences = &presentFence;
    }
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = swapchainMaintenanceExtension == nullptr ? nullptr : &presentFenceInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &chain->semaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &chain->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        throw SurfaceLost{};
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        failVk("vkQueuePresentKHR", result);
    }
    if (swapchainMaintenanceExtension != nullptr) {
        chain->presentFencePending.mut(imageIndex) = true;
    }

    const bool presented = result != VK_ERROR_OUT_OF_DATE_KHR;
    if (presented && !retiredSwapchains.empty()) {
        for (SwapchainResources* const retired : retiredSwapchains) {
            if (retired->gracePresents != 0) {
                --retired->gracePresents;
            }
        }
        collectRetiredSwapchains();
    }
    currentFrame = (currentFrame + 1) % framesInFlight;
    if (recreateAfterPresent || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
        tryCreateSwapchain(width, height);
    }
    collectRetiredSwapchains();
    return presented;
}

bool RendererImpl::repaint() {
    try {
        return repaintFrame();
    } catch (const SurfaceLost&) {
        // The surface died and this renderer can never present again.
        // Null the pointer so any stale caller crashes loudly; the object
        // itself stays alive in composer.rendererPool until frame()
        // replaces the pool — destruction happens there, safely off this
        // stack.
        composer.renderer = nullptr;
        return false;
    }
}

void RendererImpl::recordFrame(FrameResources& frame, u32 imageIndex) {
    const bool initialized = chain->direct ? chain->initialized[imageIndex] : chain->outputInitialized;
    const u64 appliedGeneration = chain->direct ? chain->generations[imageIndex] : chain->outputGeneration;
    const u32 updateCount = materializeUpdates(frame, appliedGeneration, initialized);
    const bool clearOutput = !initialized || appliedGeneration < clearDamageGeneration;
    recordCommands(frame, imageIndex, presentationState, updateCount, clearOutput);
    if (chain->direct) {
        chain->generations.mut(imageIndex) = damage.generation;
    } else {
        chain->outputGeneration = damage.generation;
        chain->outputInitialized = true;
    }
}

bool RendererImpl::repaintFrame() {
    const u32 width = renderExtent.width;
    const u32 height = renderExtent.height;
    if (!previousStateValid || cells.empty() || width == 0 || height == 0) {
        return false;
    }
    if (chain->swapchain == VK_NULL_HANDLE && !tryCreateSwapchain(width, height)) {
        return false;
    }
    if (chain->swapchain == VK_NULL_HANDLE) {
        return false;
    }

    FrameResources* frame = nullptr;
    u32 imageIndex = 0;
    bool recreateAfterPresent = false;
    if (!acquirePresentFrame(width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }

    if (!chain->direct && chain->outputInitialized) {
        recordRepaintCommands(*frame, imageIndex);
    } else {
        beginGlyphFrame();
        pinVisibleGlyphs();
        recordFrame(*frame, imageIndex);
    }
    const bool presented = submitPresentFrame(width, height, *frame, imageIndex, recreateAfterPresent);
    collectDamage();
    return presented;
}

bool RendererImpl::present(const TerminalUpdate& update) {
    const u32 width = composer.pixelWidth;
    const u32 height = composer.pixelHeight;
    const size_t cellCount = (size_t)(composer.columns) * composer.rows;
    if (cellCount == 0 || width == 0 || height == 0) {
        return false;
    }

    const bool wrongExtent = renderExtent.width != width || renderExtent.height != height;
    if ((chain->swapchain == VK_NULL_HANDLE || wrongExtent) && !tryCreateSwapchain(width, height)) {
        return false;
    }
    if (chain->swapchain == VK_NULL_HANDLE) {
        return false;
    }
    if (update.colors == nullptr) {
        return false;
    }

    const bool shapeChanged = cellColumns != composer.columns || cellRows != composer.rows;
    if (shapeChanged) {
        size_t covered = 0;
        for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
            const TerminalCellSpan& span = update.spans[spanIndex];
            if (span.cells == nullptr || span.index != covered) {
                return false;
            }
            covered += span.count;
        }
        if (covered != cellCount) {
            return false;
        }
    }

    FrameResources* frame = nullptr;
    u32 imageIndex = 0;
    bool recreateAfterPresent = false;
    if (!acquirePresentFrame(width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }

    beginGlyphFrame();
    if (previousStateValid) {
        pinVisibleGlyphs();
    }
    const u64 renderContext = mix(update.colors, composer.cellExtras, composer.fonts) ^ ((u64)(update.colors->generation) * 0x9e3779b97f4a7c15ULL);
    if (shapeChanged) {
        damage.begin = 0;
        damage.count = 0;
        ensureDamageJournal(cellCount);
        cells.clear();
        cells.grow(cellCount);
        const GpuCell empty;
        for (size_t index = 0; index < cellCount; ++index) {
            cells.pushBack(empty);
        }
        cellColumns = composer.columns;
        cellRows = composer.rows;
    } else {
        ensureDamageJournal(cellCount);
    }
    for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
        const TerminalCellSpan& span = update.spans[spanIndex];
        STD_ASSERT((size_t)(span.index) + span.count <= cellCount);
        STD_ASSERT(span.cells != nullptr);
        renderCache.render(*this, span.cells, (u16)(span.count), span.lineAttribute, cells.mutData() + span.index, *update.colors, renderContext);
    }

    if (damage.advance()) {
        clearDamageGeneration = damage.generation;
        for (u32 index = 0; index < chain->generations.length(); ++index) {
            chain->generations.mut(index) = 0;
        }
        chain->outputGeneration = 0;
    }
    const bool selectionChanged = previousStateValid && (!sameSelection(update.snappedSelection, previousSelection) || previousHoveredLinkBegin != update.hoveredLinkBegin || previousHoveredLinkEnd != update.hoveredLinkEnd);
    const bool globalPresentationChanged = previousStateValid && (presentationState.screenReverse != update.screenReverse || !(presentationState.selectionForeground == update.selectionForeground) || !(presentationState.selectionBackground == update.selectionBackground) || presentationState.selectionColorMask != update.selectionColorMask);
    // The padding follows the live default background (OSC 11); a change
    // needs a full clear, not just cell repaints.
    const bool backgroundChanged = !(clearBackground == update.colors->defaultBackground);
    clearBackground = update.colors->defaultBackground;
    if (shapeChanged || !previousStateValid || globalPresentationChanged || backgroundChanged) {
        fullDamage();
        if (shapeChanged || backgroundChanged) {
            clearDamageGeneration = damage.generation;
        }
    } else {
        if (selectionChanged) {
            // Repaint only the rows the old and new selections cover: a
            // drag must not repaint the whole grid every frame.
            const auto damageSelectionRows = [&](const Rect& selection) {
                const i32 firstRow = std::max(selection.tl.y, 0);
                const i32 lastRow = std::min<i32>(selection.br.y, (i32)(cellRows)-1);
                if (firstRow > lastRow) {
                    return;
                }
                appendDamage((u32)(firstRow)*cellColumns, (u32)(lastRow - firstRow + 1) * cellColumns);
            };
            damageSelectionRows(previousSelection);
            damageSelectionRows(update.snappedSelection);
            const auto damageLinkSpan = [&](u32 begin, u32 end) {
                if (begin < end && end <= cellCount) {
                    appendDamage(begin, end - begin);
                }
            };
            damageLinkSpan(previousHoveredLinkBegin, previousHoveredLinkEnd);
            damageLinkSpan(update.hoveredLinkBegin, update.hoveredLinkEnd);
        }
        for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
            const TerminalCellSpan& span = update.spans[spanIndex];
            appendDamage(span.index, span.count);
        }

        const auto appendCursor = [&](const TerminalCursor& cursor) {
            if (cursor.posX >= 0 && cursor.posY >= 0 && cursor.posX < cellColumns && cursor.posY < cellRows) {
                appendDamage((u32)(cursor.posY) * cellColumns + cursor.posX, 1);
            }
        };
        appendCursor(previousCursor);
        appendCursor(update.cursor);

        if (previousHoveredHyperlink != update.hoveredHyperlink) {
            for (u32 index = 0; index < cellCount; ++index) {
                const u32 hyperlink = cells[index].hyperlink;
                if (hyperlink == previousHoveredHyperlink || hyperlink == update.hoveredHyperlink) {
                    appendDamage(index, 1);
                }
            }
        }
        if (presentationState.blinkVisible != update.blinkVisible) {
            for (u32 index = 0; index < cellCount; ++index) {
                if ((cells[index].attributes & gpuBlink) != 0) {
                    appendDamage(index, 1);
                }
            }
        }
    }
    capturePresentationState(update);

    recordFrame(*frame, imageIndex);
    previousCursor = update.cursor;
    previousSelection = update.snappedSelection;
    previousHoveredHyperlink = update.hoveredHyperlink;
    previousHoveredLinkBegin = update.hoveredLinkBegin;
    previousHoveredLinkEnd = update.hoveredLinkEnd;
    previousStateValid = true;
    const bool presented = submitPresentFrame(width, height, *frame, imageIndex, recreateAfterPresent);
    collectDamage();
    return presented;
}

bool RendererImpl::update(const TerminalUpdate& update) {
    try {
        return present(update);
    } catch (const SurfaceLost&) {
        // See repaint(): mark dead, frame() rebuilds pool and renderer.
        composer.renderer = nullptr;
        return false;
    }
}

Renderer* createVulkanRenderer(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context) {
    RendererImpl* const renderer = pool.make<RendererImpl>(composer, context);
    composer.fontChangedListeners.pushBack(pool.make<CallRendererFontChanged>(renderer));
    composer.cellExtrasChangedListeners.pushBack(pool.make<CallRendererCellExtrasChanged>(renderer));
    return renderer;
}
