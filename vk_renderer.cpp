/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vk_renderer.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"

#include "log.h"
#include "options.h"
#include "render_spv.h"
#include "utf8.h"
#include "vterm.h"

#include <std/sys/crt.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace stl {}

using namespace stl;

namespace {
    struct GpuCell {
        u32 codepoint;
        u32 attributes;
        u32 foreground;
        u32 background;
        u32 underlineColor;
        u32 hyperlink;
        u32 grapheme;
        u32 semantic;
        u32 lineAttribute;
    };

    static_assert(sizeof(GpuCell) == 36, "Vulkan cell layout mismatch");

    struct RendererImpl final: public Renderer {
        RendererImpl(GLFWwindow* window, Fontpack* fontpk);
        ~RendererImpl();

        bool update(const TerminalUpdate& update) override;
        bool repaint() override;

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
            VkBuffer graphemeBuffer = VK_NULL_HANDLE;
            VkDeviceMemory graphemeMemory = VK_NULL_HANDLE;
            void* graphemes = nullptr;
            size_t graphemeCapacity = 0;
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
            i32 previousCursorX;
            i32 previousCursorY;
            u32 deltaFrame;
            u32 selectionChanged;
            u32 selectionForeground;
            u32 selectionBackground;
            u32 selectionColorMask;
            u32 blinkVisible;
            u32 cursorBlink;
        };

        static_assert(sizeof(PushConstants) == 112, "Vulkan push constant layout mismatch");

        struct GlyphSlot {
            u32 id = 0;
            u32 generation = 0;
            u8 layers = 0;
        };

        struct GlyphCache {
            stl::Buffer refs;
            stl::Vector<GlyphSlot> slots;
            u32 columns = 0;
            u32 rows = 0;
            u32 next = 1;
            u32 eviction = 1;
            u32 generation = 0;
        };

        static constexpr u32 framesInFlight = 2;

        stl::Vector<u32> graphemeScratch;
        GLFWwindow* window = nullptr;
        Fontpack* fonts = nullptr;
        u32 glyphWidth = 0;
        u32 glyphHeight = 0;
        bool hasDoubleWidth = false;

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
        VkSampler atlasSampler = VK_NULL_HANDLE;

        ImageResource atlas;
        ImageResource atlasMap;
        ImageResource doubleWidthAtlas;
        ImageResource doubleWidthAtlasMap;
        bool fontImagesInitialized = false;
        GlyphCache glyphs;
        GlyphCache doubleWidthGlyphs;
        stl::Buffer fontUploadData;
        stl::Vector<VkBufferImageCopy> atlasCopies;
        stl::Vector<VkBufferImageCopy> atlasMapCopies;
        stl::Vector<VkBufferImageCopy> doubleWidthAtlasCopies;
        stl::Vector<VkBufferImageCopy> doubleWidthAtlasMapCopies;
        ImageResource outputImage;
        bool outputInitialized = false;
        TerminalCursor previousCursor;
        Rect previousSelection;
        bool previousStateValid = false;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent{};
        VkExtent2D renderExtent{};
        std::vector<VkImage> swapchainImages;
        std::vector<VkSemaphore> presentSemaphores;
        std::vector<bool> imageInitialized;

        std::array<FrameResources, framesInFlight> frames;
        u32 currentFrame = 0;

        void createInstance();
        void selectPhysicalDevice();
        void createDevice();
        void createCommandResources();
        void createFontResources();
        void createDescriptors();
        void createPipeline();
        void createSwapchain(u32 width, u32 height);
        void destroySwapchain();
        void createOutputImage(u32 width, u32 height);
        void ensureCellBuffer(FrameResources& frame, size_t bytes);
        void ensureGraphemeBuffer(FrameResources& frame, size_t bytes);
        void ensureFontUploadBuffer(FrameResources& frame, size_t bytes);

        ImageResource createImage(u32 width, u32 height, u32 layers, VkFormat format, VkImageUsageFlags usage, bool arrayView = false);
        void destroyImage(ImageResource& image);
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;
        u32 findMemoryType(u32 allowed, VkMemoryPropertyFlags properties) const;
        void updateStaticDescriptors();
        void updateOutputDescriptors();
        void updateCellDescriptor(FrameResources& frame);
        void updateGraphemeDescriptor(FrameResources& frame);
        void beginGlyphFrame();
        void configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget, u32 maxImageDimension);
        u16 allocateGlyphSlot(GlyphCache& cache, u32 id);
        void ensureGlyph(u32 id, FontStyle style, bool doubleWidth);
        VkDeviceSize stageFontData(const void* data, size_t len, size_t expected);
        void stageAtlasMap(GlyphCache& cache, stl::Vector<VkBufferImageCopy>& copies, u32 id, u16 slot);
        void recordFontUploads(FrameResources& frame);
        void recordImageUploads(VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, const ImageResource& image, const stl::Vector<VkBufferImageCopy>& copies, bool initialize);
        void recordCommands(FrameResources& frame, u32 imageIndex, const TerminalUpdate& update, bool incremental);
        void recordRepaintCommands(FrameResources& frame, u32 imageIndex);
        bool acquirePresentFrame(u32 width, u32 height, FrameResources*& frame, u32& imageIndex, bool& recreateAfterPresent);
        bool submitPresentFrame(u32 width, u32 height, FrameResources& frame, u32 imageIndex, bool recreateAfterPresent);
        bool present(const TerminalUpdate& update, bool incrementalFrame);

        static bool needsFontGlyph(u32 id);
        static u32 packColor(const Color& color);
        static bool sameSelection(const Rect& lhs, const Rect& rhs);
    };

    [[noreturn]] void failVk(const char* operation, VkResult result) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string((int)(result)) + ")");
    }

    void checkVk(VkResult result, const char* operation) {
        if (result != VK_SUCCESS) {
            failVk(operation, result);
        }
    }

    bool deviceHasSwapchain(VkPhysicalDevice physicalDevice) {
        u32 count = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }

        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data()) != VK_SUCCESS) {
            return false;
        }

        return std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
    }

    bool formatSupports(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatFeatureFlags features) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        return (properties.optimalTilingFeatures & features) == features;
    }

    bool deviceSupportsRenderer(VkPhysicalDevice physicalDevice) {
        return formatSupports(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT) && formatSupports(physicalDevice, VK_FORMAT_R8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT) && formatSupports(physicalDevice, VK_FORMAT_R8G8_UINT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
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

    u32 packCellAttributes(const RenderCell& cell) {
        return ((u32)(cell.bold) << 2) | ((u32)(cell.italic) << 3) | ((u32)(cell.underline) << 4) | ((u32)(cell.inverse) << 5) | ((u32)(cell.wrap) << 6) | ((u32)(cell.faint) << 8) | ((u32)(cell.blink) << 9) | ((u32)(cell.conceal) << 10) | ((u32)(cell.strike) << 11) | ((u32)(cell.overline) << 12) | ((u32)(cell.underline_style) << 13) | ((u32)(cell.dwidth) << 16) | ((u32)(cell.dwidth_cont) << 17) | ((u32)(cell.dirty) << 23);
    }
}

u32 Renderer::rendererCellAttributesForTest(const RenderCell& cell) {
    return packCellAttributes(cell);
}

RendererImpl::RendererImpl(GLFWwindow* window_, Fontpack* fontpk)
    : window(window_)
    , fonts(fontpk)
    , glyphWidth(fontpk->getPx())
    , glyphHeight(fontpk->getPy())
    , hasDoubleWidth(fontpk->hasDoubleWidth())
{
    createInstance();
    checkVk(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
    selectPhysicalDevice();
    createDevice();
    createCommandResources();
    createFontResources();
    createDescriptors();
    createPipeline();
}

RendererImpl::~RendererImpl() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    destroySwapchain();
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
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

    destroyImage(outputImage);
    destroyImage(doubleWidthAtlasMap);
    destroyImage(doubleWidthAtlas);
    destroyImage(atlasMap);
    destroyImage(atlas);

    for (auto& frame : frames) {
        if (frame.cells != nullptr) {
            vkUnmapMemory(device, frame.cellMemory);
        }
        if (frame.cellBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, frame.cellBuffer, nullptr);
        }
        if (frame.cellMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, frame.cellMemory, nullptr);
        }
        if (frame.graphemes != nullptr) {
            vkUnmapMemory(device, frame.graphemeMemory);
        }
        if (frame.graphemeBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, frame.graphemeBuffer, nullptr);
        }
        if (frame.graphemeMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, frame.graphemeMemory, nullptr);
        }
        if (frame.fontUploads != nullptr) {
            vkUnmapMemory(device, frame.fontUploadMemory);
        }
        if (frame.fontUploadBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, frame.fontUploadBuffer, nullptr);
        }
        if (frame.fontUploadMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, frame.fontUploadMemory, nullptr);
        }
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
    u32 extensionCount = 0;
    const char* const* extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr) {
        const char* description = nullptr;
        glfwGetError(&description);
        throw std::runtime_error(std::string("glfwGetRequiredInstanceExtensions failed") + (description != nullptr ? ": " + std::string(description) : ""));
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
        if (!deviceHasSwapchain(candidate) || !deviceSupportsRenderer(candidate)) {
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
        std::cout << "Vulkan device: " << bestProperties.deviceName << '\n' << "Vulkan API: " << VK_VERSION_MAJOR(bestProperties.apiVersion) << '.' << VK_VERSION_MINOR(bestProperties.apiVersion) << '.' << VK_VERSION_PATCH(bestProperties.apiVersion) << '\n';
    }
}

void RendererImpl::createDevice() {
    constexpr float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
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

RendererImpl::ImageResource RendererImpl::createImage(u32 width, u32 height, u32 layers, VkFormat format, VkImageUsageFlags usage, bool arrayView) {
    ImageResource result;
    result.width = width;
    result.height = height;
    result.layers = layers;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
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
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = layers;
    checkVk(vkCreateImageView(device, &viewInfo, nullptr, &result.view), "vkCreateImageView");
    return result;
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

void RendererImpl::configureGlyphCache(GlyphCache& cache, u32 width, u32 layers, size_t byteBudget, u32 maxImageDimension) {
    constexpr u32 unicodeCount = 0x110000;
    constexpr u32 maximumSlots = 16384;
    const size_t glyphBytes = (size_t)(width)*glyphHeight * layers;
    u32 requested = glyphBytes == 0 ? 2 : (u32)(byteBudget / glyphBytes);
    if (requested < 2) {
        requested = 2;
    }
    if (requested > maximumSlots) {
        requested = maximumSlots;
    }

    u32 maximumColumns = maxImageDimension / width;
    u32 maximumRows = maxImageDimension / glyphHeight;
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
        const u64 pixelHeight = (u64)(rows)*glyphHeight;
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
    cache.refs.zero((size_t)(unicodeCount) * sizeof(u16));
    cache.slots.zero((size_t)(bestColumns)*bestRows);
}

void RendererImpl::createFontResources() {
    constexpr size_t atlasByteBudget = 16 * 1024 * 1024;
    constexpr size_t doubleWidthAtlasByteBudget = 8 * 1024 * 1024;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    configureGlyphCache(glyphs, glyphWidth, 4, atlasByteBudget, properties.limits.maxImageDimension2D);
    atlas = createImage(glyphWidth * glyphs.columns, glyphHeight * glyphs.rows, 4, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);
    atlasMap = createImage(512, 2176, 1, VK_FORMAT_R8G8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    if (hasDoubleWidth) {
        configureGlyphCache(doubleWidthGlyphs, 2 * glyphWidth, 1, doubleWidthAtlasByteBudget, properties.limits.maxImageDimension2D);
        doubleWidthAtlas = createImage(2 * glyphWidth * doubleWidthGlyphs.columns, glyphHeight * doubleWidthGlyphs.rows, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);
        doubleWidthAtlasMap = createImage(512, 2176, 1, VK_FORMAT_R8G8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }

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

void RendererImpl::createDescriptors() {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
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
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    checkVk(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout), "vkCreateDescriptorSetLayout");

    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, framesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * framesInFlight},
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
    const ImageResource& wideAtlas = hasDoubleWidth ? doubleWidthAtlas : atlas;
    const ImageResource& wideMap = hasDoubleWidth ? doubleWidthAtlasMap : atlasMap;

    for (auto& frame : frames) {
        const VkDescriptorImageInfo imageInfos[] = {
            {atlasSampler, atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, atlasMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
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

void RendererImpl::updateOutputDescriptors() {
    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, outputImage.view, VK_IMAGE_LAYOUT_GENERAL};
    for (auto& frame : frames) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
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

void RendererImpl::updateGraphemeDescriptor(FrameResources& frame) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = frame.graphemeBuffer;
    bufferInfo.range = frame.graphemeCapacity;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frame.descriptorSet;
    write.dstBinding = 6;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void RendererImpl::createPipeline() {
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = renderShaderSpvSize;
    moduleInfo.pCode = renderShaderSpv;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule), "vkCreateShaderModule");

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

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    checkVk(result, "vkCreateComputePipelines");
}

void RendererImpl::destroySwapchain() {
    for (const auto semaphore : presentSemaphores) {
        if (device != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    presentSemaphores.clear();
    swapchainImages.clear();
    imageInitialized.clear();
    if (swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = VK_NULL_HANDLE;
    swapchainFormat = VK_FORMAT_UNDEFINED;
    swapchainExtent = {};
}

void RendererImpl::createOutputImage(u32 width, u32 height) {
    destroyImage(outputImage);
    outputImage = createImage(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    renderExtent = {width, height};
    outputInitialized = false;
    previousStateValid = false;
    updateOutputDescriptors();
}

void RendererImpl::createSwapchain(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return;
    }

    checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");

    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        throw std::runtime_error("Vulkan surface cannot be used as a transfer target");
    }

    u32 formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formatCount == 0) {
        throw std::runtime_error("Vulkan surface exposes no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");

    const VkFormat preferredFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
    };
    VkSurfaceFormatKHR surfaceFormat{};
    bool formatFound = false;
    for (const auto preferred : preferredFormats) {
        const auto found = std::find_if(formats.begin(), formats.end(), [this, preferred](const VkSurfaceFormatKHR& format) {
            return format.format == preferred && formatSupports(physicalDevice, format.format, VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
        });
        if (found != formats.end()) {
            surfaceFormat = *found;
            formatFound = true;
            break;
        }
    }
    if (!formatFound) {
        throw std::runtime_error("Vulkan surface has no blit-capable 32-bit RGBA/BGRA format");
    }

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
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = selectCompositeAlpha(capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain;

    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    checkVk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &replacement), "vkCreateSwapchainKHR");
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = replacement;
    swapchainFormat = surfaceFormat.format;
    swapchainExtent = extent;

    checkVk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr), "vkGetSwapchainImagesKHR");
    swapchainImages.resize(imageCount);
    checkVk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()), "vkGetSwapchainImagesKHR");
    for (const auto semaphore : presentSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    presentSemaphores.assign(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& semaphore : presentSemaphores) {
        checkVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore");
    }
    imageInitialized.assign(imageCount, false);

    if (renderExtent.width != width || renderExtent.height != height) {
        createOutputImage(width, height);
    }

    logI << "Vulkan swapchain " << extent.width << " x " << extent.height << ", " << imageCount << " images; compute target " << renderExtent.width << " x " << renderExtent.height << std::endl;
}

void RendererImpl::ensureCellBuffer(FrameResources& frame, size_t bytes) {
    if (frame.cellCapacity >= bytes) {
        return;
    }

    if (frame.cells != nullptr) {
        vkUnmapMemory(device, frame.cellMemory);
        frame.cells = nullptr;
    }
    if (frame.cellBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, frame.cellBuffer, nullptr);
    }
    if (frame.cellMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, frame.cellMemory, nullptr);
    }

    createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.cellBuffer, frame.cellMemory);
    checkVk(vkMapMemory(device, frame.cellMemory, 0, bytes, 0, &frame.cells), "vkMapMemory");
    frame.cellCapacity = bytes;
    updateCellDescriptor(frame);
}

void RendererImpl::ensureGraphemeBuffer(FrameResources& frame, size_t bytes) {
    bytes = std::max(bytes, sizeof(u32));
    if (frame.graphemeCapacity >= bytes) {
        return;
    }

    if (frame.graphemes != nullptr) {
        vkUnmapMemory(device, frame.graphemeMemory);
        frame.graphemes = nullptr;
    }
    if (frame.graphemeBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, frame.graphemeBuffer, nullptr);
    }
    if (frame.graphemeMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, frame.graphemeMemory, nullptr);
    }

    createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.graphemeBuffer, frame.graphemeMemory);
    checkVk(vkMapMemory(device, frame.graphemeMemory, 0, bytes, 0, &frame.graphemes), "vkMapMemory");
    frame.graphemeCapacity = bytes;
    updateGraphemeDescriptor(frame);
}

void RendererImpl::ensureFontUploadBuffer(FrameResources& frame, size_t bytes) {
    if (frame.fontUploadCapacity >= bytes) {
        return;
    }
    if (frame.fontUploads != nullptr) {
        vkUnmapMemory(device, frame.fontUploadMemory);
        frame.fontUploads = nullptr;
    }
    if (frame.fontUploadBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, frame.fontUploadBuffer, nullptr);
    }
    if (frame.fontUploadMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, frame.fontUploadMemory, nullptr);
    }

    createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.fontUploadBuffer, frame.fontUploadMemory);
    checkVk(vkMapMemory(device, frame.fontUploadMemory, 0, bytes, 0, &frame.fontUploads), "vkMapMemory");
    frame.fontUploadCapacity = bytes;
}

bool RendererImpl::needsFontGlyph(u32 id) {
    return id != 0x200d && !(id >= 0xfe00 && id <= 0xfe0f) && !(id >= 0xe0100 && id <= 0xe01ef) && !(id >= 0x2500 && id <= 0x257f) && !(id >= 0x23ba && id <= 0x23bd);
}

void RendererImpl::beginGlyphFrame() {
    fontUploadData.reset();
    atlasCopies.clear();
    atlasMapCopies.clear();
    doubleWidthAtlasCopies.clear();
    doubleWidthAtlasMapCopies.clear();

    auto advance = [](GlyphCache& cache) {
        ++cache.generation;
        if (cache.generation == 0) {
            for (GlyphSlot* slot = cache.slots.mutBegin(); slot != cache.slots.mutEnd(); ++slot) {
                slot->generation = 0;
            }
            cache.generation = 1;
        }
    };
    advance(glyphs);
    if (hasDoubleWidth) {
        advance(doubleWidthGlyphs);
    }
}

u16 RendererImpl::allocateGlyphSlot(GlyphCache& cache, u32 id) {
    if (cache.next < cache.slots.length()) {
        const u16 slot = (u16)(cache.next++);
        GlyphSlot& state = cache.slots.mut(slot);
        state.id = id;
        state.generation = cache.generation;
        state.layers = 0;
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

        u16* refs = (u16*)(cache.refs.mutData());
        if (state.id < 0x110000 && refs[state.id] == slot) {
            refs[state.id] = 0;
        }
        state.id = id;
        state.generation = cache.generation;
        state.layers = 0;
        return slot;
    }
    return 0;
}

VkDeviceSize RendererImpl::stageFontData(const void* data, size_t len, size_t expected) {
    const u32 zero = 0;
    const size_t padding = (4 - (fontUploadData.used() & 3)) & 3;
    fontUploadData.append(&zero, padding);
    const VkDeviceSize offset = fontUploadData.used();
    if (data != nullptr && len == expected) {
        fontUploadData.append(data, len);
    } else {
        fontUploadData.growDelta(expected);
        void* begin = fontUploadData.mutCurrent();
        fontUploadData.seekRelative(expected);
        memZero(begin, fontUploadData.mutCurrent());
    }
    return offset;
}

void RendererImpl::stageAtlasMap(GlyphCache& cache, Vector<VkBufferImageCopy>& copies, u32 id, u16 slot) {
    const u8 position[2] = {
        (u8)(slot % cache.columns),
        (u8)(slot / cache.columns),
    };
    VkBufferImageCopy copy{};
    copy.bufferOffset = stageFontData(position, sizeof(position), sizeof(position));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {(i32)(id & 511), (i32)(id >> 9), 0};
    copy.imageExtent = {1, 1, 1};
    copies.pushBack(copy);
}

void RendererImpl::ensureGlyph(u32 id, FontStyle style, bool doubleWidth) {
    if (id >= 0x110000 || !needsFontGlyph(id) || (doubleWidth && !hasDoubleWidth)) {
        return;
    }

    GlyphCache& cache = doubleWidth ? doubleWidthGlyphs : glyphs;
    u16* refs = (u16*)(cache.refs.mutData());
    u16 slot = refs[id];
    if (slot == 0) {
        slot = allocateGlyphSlot(cache, id);
        if (slot == 0) {
            return;
        }
        refs[id] = slot;
        stageAtlasMap(cache, doubleWidth ? doubleWidthAtlasMapCopies : atlasMapCopies, id, slot);
    }

    GlyphSlot& state = cache.slots.mut(slot);
    state.generation = cache.generation;
    const u32 layer = doubleWidth ? 0 : (u32)(style);
    const u8 layerMask = (u8)(1u << layer);
    if (state.layers & layerMask) {
        return;
    }

    const u32 width = doubleWidth ? 2 * glyphWidth : glyphWidth;
    const size_t bytes = (size_t)(width)*glyphHeight;
    const FontGlyph glyph = fonts->glyph(id, style, doubleWidth);
    VkBufferImageCopy copy{};
    copy.bufferOffset = stageFontData(glyph.data, glyph.len, bytes);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.baseArrayLayer = layer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {
        (i32)((slot % cache.columns) * width),
        (i32)((slot / cache.columns) * glyphHeight),
        0,
    };
    copy.imageExtent = {width, glyphHeight, 1};
    (doubleWidth ? doubleWidthAtlasCopies : atlasCopies).pushBack(copy);
    state.layers |= layerMask;
}

void RendererImpl::recordImageUploads(VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, const ImageResource& image, const Vector<VkBufferImageCopy>& copies, bool initialize) {
    if (!initialize && copies.empty()) {
        return;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = initialize ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = initialize ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image.image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = image.layers;
    vkCmdPipelineBarrier(commandBuffer, initialize ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    if (initialize) {
        const VkClearColorValue clear{};
        vkCmdClearColorImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &toTransfer.subresourceRange);
    }
    if (!copies.empty()) {
        if (initialize) {
            VkImageMemoryBarrier clearForCopies = toTransfer;
            clearForCopies.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearForCopies.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearForCopies.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &clearForCopies);
        }
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copies.length(), copies.data());
    }

    VkImageMemoryBarrier toShader = toTransfer;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
}

void RendererImpl::recordFontUploads(FrameResources& frame) {
    const bool initialize = !fontImagesInitialized;
    if (!initialize && fontUploadData.empty()) {
        return;
    }

    if (!fontUploadData.empty()) {
        VkBufferMemoryBarrier stagingForTransfer{};
        stagingForTransfer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        stagingForTransfer.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        stagingForTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        stagingForTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stagingForTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stagingForTransfer.buffer = frame.fontUploadBuffer;
        stagingForTransfer.size = fontUploadData.used();
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &stagingForTransfer, 0, nullptr);
    }

    recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, atlas, atlasCopies, initialize);
    recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, atlasMap, atlasMapCopies, initialize);
    if (hasDoubleWidth) {
        recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, doubleWidthAtlas, doubleWidthAtlasCopies, initialize);
        recordImageUploads(frame.commandBuffer, frame.fontUploadBuffer, doubleWidthAtlasMap, doubleWidthAtlasMapCopies, initialize);
    }
    fontImagesInitialized = true;
}

u32 RendererImpl::packColor(const Color& color) {
    return (u32)(color.red) | ((u32)(color.green) << 8) | ((u32)(color.blue) << 16);
}

bool RendererImpl::sameSelection(const Rect& lhs, const Rect& rhs) {
    return lhs.tl == rhs.tl && lhs.br == rhs.br && lhs.rectangular == rhs.rectangular;
}

void RendererImpl::recordCommands(FrameResources& frame, u32 imageIndex, const TerminalUpdate& update, bool incremental) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    recordFontUploads(frame);

    VkImageSubresourceRange outputRange{};
    outputRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputRange.levelCount = 1;
    outputRange.layerCount = 1;
    VkImageMemoryBarrier outputForCompute{};
    outputForCompute.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputForCompute.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputForCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputForCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForCompute.image = outputImage.image;
    outputForCompute.subresourceRange = outputRange;
    if (incremental) {
        outputForCompute.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        outputForCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &outputForCompute);
    } else {
        VkImageMemoryBarrier outputForClear = outputForCompute;
        outputForClear.srcAccessMask = outputInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        outputForClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        outputForClear.oldLayout = outputInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        vkCmdPipelineBarrier(frame.commandBuffer, outputInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &outputForClear);

        VkClearColorValue clearColor{{
            opts.bg.red / 255.0f,
            opts.bg.green / 255.0f,
            opts.bg.blue / 255.0f,
            1.0f,
        }};
        vkCmdClearColorImage(frame.commandBuffer, outputImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &outputRange);

        outputForCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        outputForCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &outputForCompute);
    }

    VkBufferMemoryBarrier cellsForCompute{};
    cellsForCompute.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    cellsForCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    cellsForCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    cellsForCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cellsForCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cellsForCompute.buffer = frame.cellBuffer;
    cellsForCompute.size = frame.cellCapacity;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &cellsForCompute, 0, nullptr);

    VkBufferMemoryBarrier graphemesForCompute = cellsForCompute;
    graphemesForCompute.buffer = frame.graphemeBuffer;
    graphemesForCompute.size = frame.graphemeCapacity;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &graphemesForCompute, 0, nullptr);

    const PushConstants pushConstants{
        glyphWidth,
        glyphHeight,
        update.columns,
        update.rows,
        update.pixelWidth,
        update.pixelHeight,
        opts.border,
        packColor(update.cursor.color),
        update.cursor.posX,
        update.cursor.posY,
        (u32)(update.cursor.style),
        update.screenReverse ? 1u : 0u,
        update.snappedSelection.tl.x,
        update.snappedSelection.tl.y,
        update.snappedSelection.br.x,
        update.snappedSelection.br.y,
        update.snappedSelection.rectangular ? 1u : 0u,
        opts.showWraps ? 1u : 0u,
        hasDoubleWidth ? 1u : 0u,
        previousCursor.posX,
        previousCursor.posY,
        incremental ? 1u : 0u,
        !sameSelection(update.snappedSelection, previousSelection) ? 1u : 0u,
        packColor(update.selectionForeground),
        packColor(update.selectionBackground),
        update.selectionColorMask,
        update.blinkVisible ? 1u : 0u,
        update.cursorBlink ? 1u : 0u,
    };
    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
    vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDispatch(frame.commandBuffer, (update.columns + 7) / 8, (update.rows + 7) / 8, 1);

    VkImageMemoryBarrier outputForBlit = outputForCompute;
    outputForBlit.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputForBlit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &outputForBlit);

    VkImageMemoryBarrier swapchainForBlit{};
    swapchainForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapchainForBlit.srcAccessMask = imageInitialized[imageIndex] ? VK_ACCESS_MEMORY_READ_BIT : 0;
    swapchainForBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForBlit.oldLayout = imageInitialized[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainForBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.image = swapchainImages[imageIndex];
    swapchainForBlit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapchainForBlit.subresourceRange.levelCount = 1;
    swapchainForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(frame.commandBuffer, imageInitialized[imageIndex] ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainForBlit);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {(i32)(renderExtent.width), (i32)(renderExtent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {(i32)(swapchainExtent.width), (i32)(swapchainExtent.height), 1};
    vkCmdBlitImage(frame.commandBuffer, outputImage.image, VK_IMAGE_LAYOUT_GENERAL, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    VkImageMemoryBarrier swapchainForPresent = swapchainForBlit;
    swapchainForPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapchainForPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainForPresent);
    checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
}

void RendererImpl::recordRepaintCommands(FrameResources& frame, u32 imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    VkImageMemoryBarrier outputForBlit{};
    outputForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputForBlit.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    outputForBlit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    outputForBlit.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputForBlit.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForBlit.image = outputImage.image;
    outputForBlit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputForBlit.subresourceRange.levelCount = 1;
    outputForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &outputForBlit);

    VkImageMemoryBarrier swapchainForBlit{};
    swapchainForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapchainForBlit.srcAccessMask = imageInitialized[imageIndex] ? VK_ACCESS_MEMORY_READ_BIT : 0;
    swapchainForBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForBlit.oldLayout = imageInitialized[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainForBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.image = swapchainImages[imageIndex];
    swapchainForBlit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapchainForBlit.subresourceRange.levelCount = 1;
    swapchainForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(frame.commandBuffer, imageInitialized[imageIndex] ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainForBlit);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {(i32)(renderExtent.width), (i32)(renderExtent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {(i32)(swapchainExtent.width), (i32)(swapchainExtent.height), 1};
    vkCmdBlitImage(frame.commandBuffer, outputImage.image, VK_IMAGE_LAYOUT_GENERAL, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    VkImageMemoryBarrier swapchainForPresent = swapchainForBlit;
    swapchainForPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapchainForPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainForPresent);
    checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
}

bool RendererImpl::acquirePresentFrame(u32 width, u32 height, FrameResources*& frame, u32& imageIndex, bool& recreateAfterPresent) {
    frame = &frames[currentFrame];
    checkVk(vkWaitForFences(device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame->imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        createSwapchain(width, height);
        return false;
    }
    recreateAfterPresent = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        failVk("vkAcquireNextImageKHR", result);
    }
    checkVk(vkResetFences(device, 1, &frame->fence), "vkResetFences");
    checkVk(vkResetCommandBuffer(frame->commandBuffer, 0), "vkResetCommandBuffer");
    return true;
}

bool RendererImpl::submitPresentFrame(u32 width, u32 height, FrameResources& frame, u32 imageIndex, bool recreateAfterPresent) {
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &presentSemaphores[imageIndex];
    checkVk(vkQueueSubmit(queue, 1, &submitInfo, frame.fence), "vkQueueSubmit");
    imageInitialized[imageIndex] = true;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        failVk("vkQueuePresentKHR", result);
    }

    const bool presented = result != VK_ERROR_OUT_OF_DATE_KHR;
    currentFrame = (currentFrame + 1) % framesInFlight;
    if (recreateAfterPresent || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
        createSwapchain(width, height);
    }
    return presented;
}

bool RendererImpl::repaint() {
    const u32 width = renderExtent.width;
    const u32 height = renderExtent.height;
    if (!outputInitialized || width == 0 || height == 0) {
        return false;
    }
    if (swapchain == VK_NULL_HANDLE) {
        createSwapchain(width, height);
    }
    if (swapchain == VK_NULL_HANDLE) {
        return false;
    }

    FrameResources* frame = nullptr;
    u32 imageIndex = 0;
    bool recreateAfterPresent = false;
    if (!acquirePresentFrame(width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }
    recordRepaintCommands(*frame, imageIndex);
    return submitPresentFrame(width, height, *frame, imageIndex, recreateAfterPresent);
}

bool RendererImpl::present(const TerminalUpdate& update, bool incrementalFrame) {
    const u32 width = update.pixelWidth;
    const u32 height = update.pixelHeight;
    if (update.cells == nullptr || update.cellCount == 0 || width == 0 || height == 0) {
        return false;
    }

    if (swapchain == VK_NULL_HANDLE || renderExtent.width != width || renderExtent.height != height) {
        createSwapchain(width, height);
    }
    if (swapchain == VK_NULL_HANDLE) {
        return false;
    }

    incrementalFrame = incrementalFrame && outputInitialized && previousStateValid;

    FrameResources* frame = nullptr;
    u32 imageIndex = 0;
    bool recreateAfterPresent = false;
    if (!acquirePresentFrame(width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }

    beginGlyphFrame();
    const size_t cellBytes = update.cellCount * sizeof(GpuCell);
    ensureCellBuffer(*frame, cellBytes);
    GpuCell* const gpuCells = (GpuCell*)(frame->cells);
    graphemeScratch.clear();
    graphemeScratch.pushBack(0);
    CellExtraStore* const extras = update.cellExtras;
    for (size_t index = 0; index < update.cellCount; ++index) {
        const RenderCell& cell = update.cells[index];
        u32 graphemeIndex = 0;
        if (cell.grapheme) {
            const GraphemeView grapheme = extras->grapheme(cell.grapheme);
            if (!grapheme.empty()) {
                graphemeIndex = (u32)(graphemeScratch.length());
                graphemeScratch.pushBack((u32)(grapheme.size()));
                graphemeScratch.append(grapheme.begin(), grapheme.end());
                const FontStyle style = (FontStyle)((cell.bold ? 1 : 0) | (cell.italic ? 2 : 0));
                const bool doubleWidth = cell.dwidth || cell.line_attr != 0;
                for (size_t member = 0; member < grapheme.size(); ++member) {
                    ensureGlyph(grapheme[member], style, member == 0 && doubleWidth);
                }
            }
        }
        if (graphemeIndex == 0 && (!cell.dwidth_cont || cell.line_attr != 0)) {
            const FontStyle style = (FontStyle)((cell.bold ? 1 : 0) | (cell.italic ? 2 : 0));
            ensureGlyph(cell.uc_pt, style, cell.dwidth || cell.line_attr != 0);
        }
        gpuCells[index] = {
            cell.uc_pt,
            packCellAttributes(cell),
            packColor(cell.fg),
            packColor(cell.bg),
            packColor(cell.underline_color),
            cell.hyperlink,
            graphemeIndex,
            cell.semantic,
            cell.line_attr,
        };
    }

    const size_t graphemeBytes = graphemeScratch.length() * sizeof(u32);
    ensureGraphemeBuffer(*frame, graphemeBytes);
    std::memcpy(frame->graphemes, graphemeScratch.data(), graphemeBytes);
    if (!fontUploadData.empty()) {
        ensureFontUploadBuffer(*frame, fontUploadData.used());
        std::memcpy(frame->fontUploads, fontUploadData.data(), fontUploadData.used());
    }

    recordCommands(*frame, imageIndex, update, incrementalFrame);
    outputInitialized = true;
    previousCursor = update.cursor;
    previousSelection = update.snappedSelection;
    previousStateValid = true;
    return submitPresentFrame(width, height, *frame, imageIndex, recreateAfterPresent);
}

bool RendererImpl::update(const TerminalUpdate& update) {
    return present(update, update.incremental);
}

Renderer* Renderer::create(Composer& composer, GLFWwindow* window) {
    return composer.pool->make<RendererImpl>(window, composer.fonts);
}
