/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "vk_presenter.h"

#include "frame.h"

#include "log.h"
#include "options.h"
#include "render_spv.h"
#include "utf8.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
    struct GpuCell {
        u32 codepoint;
        u32 attributes;
        u32 foreground;
        u32 background;
        u32 underlineColor;
        u32 hyperlink;
        u32 grapheme;
        i32 foregroundIndex;
        i32 backgroundIndex;
        i32 underlineIndex;
        u32 semantic;
        u32 lineAttribute;
    };
    static_assert(sizeof(GpuCell) == 48,
                  "Vulkan cell layout mismatch");

    [[noreturn]] void
    failVk(const char* operation, VkResult result) {
        throw std::runtime_error(
            std::string(operation) + " failed (VkResult " +
            std::to_string((int)(result)) + ")");
    }

    void
    checkVk(VkResult result, const char* operation) {
        if (result != VK_SUCCESS) {
            failVk(operation, result);
        }
    }

    bool
    deviceHasSwapchain(VkPhysicalDevice physicalDevice) {
        u32 count = 0;
        if (vkEnumerateDeviceExtensionProperties(
                physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }

        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(
                physicalDevice, nullptr, &count, extensions.data()) !=
            VK_SUCCESS) {
            return false;
        }

        return std::any_of(
            extensions.begin(), extensions.end(),
            [](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName,
                               VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
    }

    bool
    formatSupports(VkPhysicalDevice physicalDevice, VkFormat format,
                   VkFormatFeatureFlags features) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, format, &properties);
        return (properties.optimalTilingFeatures & features) == features;
    }

    bool
    deviceSupportsRenderer(VkPhysicalDevice physicalDevice) {
        return formatSupports(
                   physicalDevice, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                       VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                       VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                       VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
               formatSupports(
                   physicalDevice, VK_FORMAT_R8_UNORM,
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                       VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
               formatSupports(
                   physicalDevice, VK_FORMAT_R8G8_UINT,
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                       VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    }

    VkCompositeAlphaFlagBitsKHR
    selectCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
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
        throw std::runtime_error(
            "Vulkan surface has no composite alpha mode");
    }
}

u32 VulkanPresenter::packCellAttributes(const TerminalCell& cell) {
    return
        ((u32)(cell.bold) << 2) |
        ((u32)(cell.italic) << 3) |
        ((u32)(cell.underline) << 4) |
        ((u32)(cell.inverse) << 5) |
        ((u32)(cell.wrap) << 6) |
        ((u32)(cell.faint) << 8) |
        ((u32)(cell.blink) << 9) |
        ((u32)(cell.conceal) << 10) |
        ((u32)(cell.strike) << 11) |
        ((u32)(cell.overline) << 12) |
        ((u32)(cell.underline_style) << 13) |
        ((u32)(cell.dwidth) << 16) |
        ((u32)(cell.dwidth_cont) << 17) |
        ((u32)(cell.dirty) << 23);
}

VulkanPresenter::VulkanPresenter(
    GLFWwindow* window_, Fontpack* fontpk)
    : window(window_)
    , glyphWidth(fontpk->getPx())
    , glyphHeight(fontpk->getPy())
    , hasDoubleWidth(fontpk->hasDoubleWidth())
{
    createInstance();
    checkVk(glfwCreateWindowSurface(
                instance, window, nullptr, &surface),
            "glfwCreateWindowSurface");
    selectPhysicalDevice();
    createDevice();
    createCommandResources();
    createFontResources(fontpk);
    createDescriptors();
    createPipeline();
    fontpk->releaseFonts();
}

VulkanPresenter::~VulkanPresenter() {
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
        vkDestroyDescriptorSetLayout(
            device, descriptorSetLayout, nullptr);
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

void VulkanPresenter::createInstance() {
    u32 extensionCount = 0;
    const char* const* extensions =
        glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr) {
        const char* description = nullptr;
        glfwGetError(&description);
        throw std::runtime_error(
            std::string("glfwGetRequiredInstanceExtensions failed") +
            (description != nullptr ? ": " + std::string(description) : ""));
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "zutty";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 14, 0);
    appInfo.pEngineName = "zutty";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance),
            "vkCreateInstance");
}

void VulkanPresenter::selectPhysicalDevice() {
    u32 deviceCount = 0;
    checkVk(vkEnumeratePhysicalDevices(
                instance, &deviceCount, nullptr),
            "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVk(vkEnumeratePhysicalDevices(
                instance, &deviceCount, devices.data()),
            "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    VkPhysicalDeviceProperties bestProperties{};
    for (const auto candidate : devices) {
        if (!deviceHasSwapchain(candidate) ||
            !deviceSupportsRenderer(candidate)) {
            continue;
        }

        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            candidate, &familyCount, families.data());

        for (u32 family = 0; family < familyCount; ++family) {
            VkBool32 presentSupported = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                        candidate, family, surface, &presentSupported),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            const VkQueueFlags required =
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[family].queueFlags & required) != required ||
                !presentSupported) {
                continue;
            }

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score =
                properties.deviceType ==
                        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                    ? 100
                : properties.deviceType ==
                        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                    ? 50
                    : 10;
            if (score > bestScore)
            {
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
            "presentation");
    }

    if (opts.vulkanInfo) {
        std::cout << "Vulkan device: " << bestProperties.deviceName << '\n'
                  << "Vulkan API: "
                  << VK_VERSION_MAJOR(bestProperties.apiVersion) << '.'
                  << VK_VERSION_MINOR(bestProperties.apiVersion) << '.'
                  << VK_VERSION_PATCH(bestProperties.apiVersion) << '\n';
    }
}

void VulkanPresenter::createDevice() {
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
    checkVk(vkCreateDevice(
                physicalDevice, &createInfo, nullptr, &device),
            "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
}

void VulkanPresenter::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    checkVk(vkCreateCommandPool(
                device, &poolInfo, nullptr, &commandPool),
            "vkCreateCommandPool");

    std::array<VkCommandBuffer, framesInFlight> commandBuffers{};
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = framesInFlight;
    checkVk(vkAllocateCommandBuffers(
                device, &allocateInfo, commandBuffers.data()),
            "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (u32 i = 0; i < framesInFlight; ++i) {
        frames[i].commandBuffer = commandBuffers[i];
        checkVk(vkCreateSemaphore(
                    device, &semaphoreInfo, nullptr,
                    &frames[i].imageAvailable),
                "vkCreateSemaphore");
        checkVk(vkCreateFence(
                    device, &fenceInfo, nullptr, &frames[i].fence),
                "vkCreateFence");
    }
}

u32
VulkanPresenter::findMemoryType(
    u32 allowed, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(
        physicalDevice, &memoryProperties);
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type found");
}

void VulkanPresenter::createBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer& buffer,
    VkDeviceMemory& memory) const {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer),
            "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits, properties);
    checkVk(vkAllocateMemory(
                device, &allocationInfo, nullptr, &memory),
            "vkAllocateMemory");
    checkVk(vkBindBufferMemory(device, buffer, memory, 0),
            "vkBindBufferMemory");
}

VulkanPresenter::ImageResource
VulkanPresenter::createImage(
    u32 width, u32 height, u32 layers,
    VkFormat format, VkImageUsageFlags usage, bool arrayView) {
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
    checkVk(vkCreateImage(
                device, &imageInfo, nullptr, &result.image),
            "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, result.image, &requirements);
    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVk(vkAllocateMemory(
                device, &allocationInfo, nullptr, &result.memory),
            "vkAllocateMemory");
    checkVk(vkBindImageMemory(
                device, result.image, result.memory, 0),
            "vkBindImageMemory");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = arrayView
                            ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                            : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = layers;
    checkVk(vkCreateImageView(
                device, &viewInfo, nullptr, &result.view),
            "vkCreateImageView");
    return result;
}

void VulkanPresenter::destroyImage(ImageResource& image) {
    if (device != VK_NULL_HANDLE && image.view != VK_NULL_HANDLE)
                        {
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

void VulkanPresenter::uploadImage(
    const ImageResource& image, const void* data, size_t bytes) {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(
        bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* mapped = nullptr;
    checkVk(vkMapMemory(
                device, stagingMemory, 0, bytes, 0, &mapped),
            "vkMapMemory");
    std::memcpy(mapped, data, bytes);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(
                device, &allocateInfo, &commandBuffer),
            "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer");

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image.image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = image.layers;
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = image.layers;
    copy.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(
        commandBuffer, stagingBuffer, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShader = toTransfer;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &toShader);

    checkVk(vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    checkVk(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE),
            "vkQueueSubmit");
    checkVk(vkQueueWaitIdle(queue), "vkQueueWaitIdle");

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

std::vector<u8>
VulkanPresenter::makeAtlasMap(const Font& font) {
    constexpr u32 unicodeCount = 0x110000;
    std::vector<u8> result(2 * unicodeCount, 0);
    const auto end = font.getAtlasMap().end();

    Font::AtlasPos replacement{};
    const auto replacementIt = font.getAtlasMap().find(
        Unicode_Replacement_Character);
    if (replacementIt != end) {
        replacement = replacementIt->second;
    }

    Font::AtlasPos missing{};
    const auto missingIt = font.getAtlasMap().find(
        Missing_Glyph_Marker);
    if (missingIt != end) {
        missing = missingIt->second;
    }

    for (u32 codepoint = 0; codepoint < unicodeCount; ++codepoint) {
        const auto& position =
            (codepoint >= 0xd800 && codepoint < 0xe000) ||
                    codepoint >= 0xfffe
                ? replacement
                : missing;
        result[2 * codepoint] = position.x;
        result[2 * codepoint + 1] = position.y;
    }

    for (const auto& entry : font.getAtlasMap()) {
        if (entry.first < unicodeCount) {
            result[2 * entry.first] = entry.second.x;
            result[2 * entry.first + 1] = entry.second.y;
        }
    }
    return result;
}

void VulkanPresenter::createFontResources(Fontpack* fontpk) {
    const Font& regular = fontpk->getRegular();
    const size_t layerBytes = regular.getAtlas().size();
    std::vector<u8> atlasData(4 * layerBytes);

    const Font* layers[4] = {
        &regular,
        fontpk->hasBold() ? &fontpk->getBold() : &regular,
        fontpk->hasItalic() ? &fontpk->getItalic() : &regular,
        fontpk->hasBoldItalic()
            ? &fontpk->getBoldItalic()
        : fontpk->hasItalic()
            ? &fontpk->getItalic()
        : fontpk->hasBold()
            ? &fontpk->getBold()
            : &regular,
    };
    for (size_t layer = 0; layer < 4; ++layer) {
        if (layers[layer]->getAtlas().size() != layerBytes)
            {
            throw std::runtime_error("Font atlas layer size mismatch");
        }
        std::memcpy(
            atlasData.data() + layer * layerBytes,
            layers[layer]->getAtlasData(), layerBytes);
    }

    atlas = createImage(
        regular.getPx() * regular.getNx(),
        regular.getPy() * regular.getNy(), 4,
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        true);
    uploadImage(atlas, atlasData.data(), atlasData.size());

    const auto mapData = makeAtlasMap(regular);
    atlasMap = createImage(
        512, 2176, 1, VK_FORMAT_R8G8_UINT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);
    uploadImage(atlasMap, mapData.data(), mapData.size());

    if (hasDoubleWidth) {
        const Font& wide = fontpk->getDoubleWidth();
        doubleWidthAtlas = createImage(
            wide.getPx() * wide.getNx(),
            wide.getPy() * wide.getNy(), 1,
            VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            true);
        uploadImage(
            doubleWidthAtlas, wide.getAtlasData(),
            wide.getAtlas().size());

        const auto wideMapData = makeAtlasMap(wide);
        doubleWidthAtlasMap = createImage(
            512, 2176, 1, VK_FORMAT_R8G8_UINT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT);
        uploadImage(
            doubleWidthAtlasMap, wideMapData.data(),
            wideMapData.size());
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
    checkVk(vkCreateSampler(
                device, &samplerInfo, nullptr, &atlasSampler),
            "vkCreateSampler");
}

void VulkanPresenter::createDescriptors() {
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
        bindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    checkVk(vkCreateDescriptorSetLayout(
                device, &layoutInfo, nullptr, &descriptorSetLayout),
            "vkCreateDescriptorSetLayout");

    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, framesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * framesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         4 * framesInFlight},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = framesInFlight;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    checkVk(vkCreateDescriptorPool(
                device, &poolInfo, nullptr, &descriptorPool),
            "vkCreateDescriptorPool");

    std::array<VkDescriptorSetLayout, framesInFlight> layouts;
    layouts.fill(descriptorSetLayout);
    std::array<VkDescriptorSet, framesInFlight> sets{};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = framesInFlight;
    allocateInfo.pSetLayouts = layouts.data();
    checkVk(vkAllocateDescriptorSets(
                device, &allocateInfo, sets.data()),
            "vkAllocateDescriptorSets");
    for (u32 i = 0; i < framesInFlight; ++i) {
        frames[i].descriptorSet = sets[i];
    }
    updateStaticDescriptors();
}

void VulkanPresenter::updateStaticDescriptors() {
    const ImageResource& wideAtlas = hasDoubleWidth
                                         ? doubleWidthAtlas
                                         : atlas;
    const ImageResource& wideMap = hasDoubleWidth
                                       ? doubleWidthAtlasMap
                                       : atlasMap;

    for (auto& frame : frames)
                                     {
        const VkDescriptorImageInfo imageInfos[] = {
            {atlasSampler, atlas.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, atlasMap.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideAtlas.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {atlasSampler, wideMap.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (u32 i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.descriptorSet;
            writes[i].dstBinding = i + 2;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(
            device, writes.size(), writes.data(), 0, nullptr);
    }
}

void VulkanPresenter::updateOutputDescriptors() {
    const VkDescriptorImageInfo imageInfo{
        VK_NULL_HANDLE, outputImage.view, VK_IMAGE_LAYOUT_GENERAL};
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

void VulkanPresenter::updateCellDescriptor(FrameResources& frame) {
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

void VulkanPresenter::updateGraphemeDescriptor(FrameResources& frame) {
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

void VulkanPresenter::createPipeline() {
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = renderShaderSpvSize;
    moduleInfo.pCode = renderShaderSpv;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(
                device, &moduleInfo, nullptr, &shaderModule),
            "vkCreateShaderModule");

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    checkVk(vkCreatePipelineLayout(
                device, &layoutInfo, nullptr, &pipelineLayout),
            "vkCreatePipelineLayout");

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType =
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;
    const VkResult result = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    checkVk(result, "vkCreateComputePipelines");
}

void VulkanPresenter::destroySwapchain() {
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

void VulkanPresenter::createOutputImage(u32 width, u32 height) {
    destroyImage(outputImage);
    outputImage = createImage(
        width, height, 1, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    renderExtent = {width, height};
    outputInitialized = false;
    previousStateValid = false;
    updateOutputDescriptors();
}

void VulkanPresenter::createSwapchain(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return;
    }

    checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");

    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!(capabilities.supportedUsageFlags &
          VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        throw std::runtime_error(
            "Vulkan surface cannot be used as a transfer target");
    }

    u32 formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice, surface, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formatCount == 0) {
        throw std::runtime_error("Vulkan surface exposes no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice, surface, &formatCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");

    const VkFormat preferredFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
    };
    VkSurfaceFormatKHR surfaceFormat{};
    bool formatFound = false;
    for (const auto preferred : preferredFormats) {
        const auto found = std::find_if(
            formats.begin(), formats.end(),
            [this, preferred](const VkSurfaceFormatKHR& format) {
            return format.format == preferred &&
                   formatSupports(
                       physicalDevice, format.format,
                       VK_FORMAT_FEATURE_BLIT_DST_BIT |
                           VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
        });
        if (found != formats.end()) {
            surfaceFormat = *found;
            formatFound = true;
            break;
        }
    }
    if (!formatFound) {
        throw std::runtime_error(
            "Vulkan surface has no blit-capable 32-bit RGBA/BGRA format");
    }

    u32 presentModeCount = 0;
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &presentModeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &presentModeCount,
                presentModes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(presentModes.begin(), presentModes.end(),
                  VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    VkExtent2D extent{};
    if (capabilities.currentExtent.width !=
        std::numeric_limits<u32>::max()) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::clamp(
            width, capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        extent.height = std::clamp(
            height, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
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
    createInfo.compositeAlpha =
        selectCompositeAlpha(capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain;

    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    checkVk(vkCreateSwapchainKHR(
                device, &createInfo, nullptr, &replacement),
            "vkCreateSwapchainKHR");
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = replacement;
    swapchainFormat = surfaceFormat.format;
    swapchainExtent = extent;

    checkVk(vkGetSwapchainImagesKHR(
                device, swapchain, &imageCount, nullptr),
            "vkGetSwapchainImagesKHR");
    swapchainImages.resize(imageCount);
    checkVk(vkGetSwapchainImagesKHR(
                device, swapchain, &imageCount,
                swapchainImages.data()),
            "vkGetSwapchainImagesKHR");
    for (const auto semaphore : presentSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    presentSemaphores.assign(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& semaphore : presentSemaphores) {
        checkVk(vkCreateSemaphore(
                    device, &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore");
    }
    imageInitialized.assign(imageCount, false);

    if (renderExtent.width != width || renderExtent.height != height) {
        createOutputImage(width, height);
    }

    logI << "Vulkan swapchain " << extent.width << " x " << extent.height
         << ", " << imageCount << " images; compute target "
         << renderExtent.width << " x " << renderExtent.height
         << std::endl;
}

void VulkanPresenter::ensureCellBuffer(
    FrameResources& frame, size_t bytes) {
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

    createBuffer(
        bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        frame.cellBuffer, frame.cellMemory);
    checkVk(vkMapMemory(
                device, frame.cellMemory, 0, bytes, 0, &frame.cells),
            "vkMapMemory");
    frame.cellCapacity = bytes;
    updateCellDescriptor(frame);
}

void VulkanPresenter::ensureGraphemeBuffer(
    FrameResources& frame, size_t bytes) {
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

    createBuffer(
        bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        frame.graphemeBuffer, frame.graphemeMemory);
    checkVk(vkMapMemory(
                device, frame.graphemeMemory, 0, bytes, 0,
                &frame.graphemes),
            "vkMapMemory");
    frame.graphemeCapacity = bytes;
    updateGraphemeDescriptor(frame);
}

u32
VulkanPresenter::packColor(const Color& color) {
    return (u32)(color.red) |
           ((u32)(color.green) << 8) |
           ((u32)(color.blue) << 16);
}

bool VulkanPresenter::sameSelection(const Rect& lhs, const Rect& rhs) {
    return lhs.tl == rhs.tl && lhs.br == rhs.br &&
           lhs.rectangular == rhs.rectangular;
}

void VulkanPresenter::recordCommands(
    FrameResources& frame, u32 imageIndex,
    const CharVdev& charVdev, const Frame& sourceFrame, bool delta) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo),
            "vkBeginCommandBuffer");

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
    if (delta) {
        outputForCompute.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        outputForCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &outputForCompute);
    } else {
        VkImageMemoryBarrier outputForClear = outputForCompute;
        outputForClear.srcAccessMask = outputInitialized
                                           ? VK_ACCESS_TRANSFER_READ_BIT
                                           : 0;
        outputForClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        outputForClear.oldLayout = outputInitialized
                                       ? VK_IMAGE_LAYOUT_GENERAL
                                       : VK_IMAGE_LAYOUT_UNDEFINED;
        vkCmdPipelineBarrier(
            frame.commandBuffer,
            outputInitialized
                ? VK_PIPELINE_STAGE_TRANSFER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &outputForClear);

        VkClearColorValue clearColor{{
            opts.bg.red / 255.0f,
            opts.bg.green / 255.0f,
            opts.bg.blue / 255.0f,
            1.0f,
        }};
        vkCmdClearColorImage(
            frame.commandBuffer, outputImage.image,
            VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &outputRange);

        outputForCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        outputForCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &outputForCompute);
    }

    VkBufferMemoryBarrier cellsForCompute{};
    cellsForCompute.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    cellsForCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    cellsForCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    cellsForCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cellsForCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cellsForCompute.buffer = frame.cellBuffer;
    cellsForCompute.size = frame.cellCapacity;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        0, nullptr, 1, &cellsForCompute, 0, nullptr);

    VkBufferMemoryBarrier graphemesForCompute = cellsForCompute;
    graphemesForCompute.buffer = frame.graphemeBuffer;
    graphemesForCompute.size = frame.graphemeCapacity;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        0, nullptr, 1, &graphemesForCompute, 0, nullptr);

    const auto& cursor = charVdev.getCursor();
    const auto& selection = charVdev.getSelection();
    const PushConstants pushConstants{
        glyphWidth,
        glyphHeight,
        charVdev.columns(),
        charVdev.rows(),
        charVdev.pixelWidth(),
        charVdev.pixelHeight(),
        opts.border,
        packColor(cursor.color),
        cursor.posX,
        cursor.posY,
        (u32)(cursor.style),
        sourceFrame.getScreenReverseVideo() ? 1u : 0u,
        selection.tl.x,
        selection.tl.y,
        selection.br.x,
        selection.br.y,
        selection.rectangular ? 1u : 0u,
        opts.showWraps ? 1u : 0u,
        hasDoubleWidth ? 1u : 0u,
        previousCursor.posX,
        previousCursor.posY,
        delta ? 1u : 0u,
        !sameSelection(selection, previousSelection) ? 1u : 0u,
        packColor(sourceFrame.getSelectionForeground()),
        packColor(sourceFrame.getSelectionBackground()),
        sourceFrame.getSelectionColorMask(),
        sourceFrame.getBlinkVisible() ? 1u : 0u,
        sourceFrame.getCursorBlink() ? 1u : 0u,
    };
    vkCmdBindPipeline(
        frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
    vkCmdPushConstants(
        frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(pushConstants), &pushConstants);
    vkCmdDispatch(
        frame.commandBuffer,
        (charVdev.columns() + 7) / 8,
        (charVdev.rows() + 7) / 8, 1);

    VkImageMemoryBarrier outputForBlit = outputForCompute;
    outputForBlit.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputForBlit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &outputForBlit);

    VkImageMemoryBarrier swapchainForBlit{};
    swapchainForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapchainForBlit.srcAccessMask = imageInitialized[imageIndex]
                                         ? VK_ACCESS_MEMORY_READ_BIT
                                         : 0;
    swapchainForBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForBlit.oldLayout = imageInitialized[imageIndex]
                                     ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                     : VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainForBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.image = swapchainImages[imageIndex];
    swapchainForBlit.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    swapchainForBlit.subresourceRange.levelCount = 1;
    swapchainForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        frame.commandBuffer,
        imageInitialized[imageIndex]
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &swapchainForBlit);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {
        (i32)(renderExtent.width),
        (i32)(renderExtent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {
        (i32)(swapchainExtent.width),
        (i32)(swapchainExtent.height), 1};
    vkCmdBlitImage(
        frame.commandBuffer,
        outputImage.image, VK_IMAGE_LAYOUT_GENERAL,
        swapchainImages[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_NEAREST);

    VkImageMemoryBarrier swapchainForPresent = swapchainForBlit;
    swapchainForPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapchainForPresent.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &swapchainForPresent);
    checkVk(vkEndCommandBuffer(frame.commandBuffer),
            "vkEndCommandBuffer");
}

void VulkanPresenter::recordRepaintCommands(
    FrameResources& frame, u32 imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo),
            "vkBeginCommandBuffer");

    VkImageMemoryBarrier outputForBlit{};
    outputForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputForBlit.srcAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    outputForBlit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    outputForBlit.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputForBlit.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputForBlit.image = outputImage.image;
    outputForBlit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputForBlit.subresourceRange.levelCount = 1;
    outputForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &outputForBlit);

    VkImageMemoryBarrier swapchainForBlit{};
    swapchainForBlit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapchainForBlit.srcAccessMask = imageInitialized[imageIndex]
                                         ? VK_ACCESS_MEMORY_READ_BIT
                                         : 0;
    swapchainForBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForBlit.oldLayout = imageInitialized[imageIndex]
                                     ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                     : VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainForBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapchainForBlit.image = swapchainImages[imageIndex];
    swapchainForBlit.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    swapchainForBlit.subresourceRange.levelCount = 1;
    swapchainForBlit.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        frame.commandBuffer,
        imageInitialized[imageIndex]
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &swapchainForBlit);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {
        (i32)(renderExtent.width),
        (i32)(renderExtent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {
        (i32)(swapchainExtent.width),
        (i32)(swapchainExtent.height), 1};
    vkCmdBlitImage(
        frame.commandBuffer,
        outputImage.image, VK_IMAGE_LAYOUT_GENERAL,
        swapchainImages[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_NEAREST);

    VkImageMemoryBarrier swapchainForPresent = swapchainForBlit;
    swapchainForPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapchainForPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapchainForPresent.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapchainForPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(
        frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &swapchainForPresent);
    checkVk(vkEndCommandBuffer(frame.commandBuffer),
            "vkEndCommandBuffer");
}

bool VulkanPresenter::acquirePresentFrame(
    u32 width, u32 height, FrameResources*& frame,
    u32& imageIndex, bool& recreateAfterPresent) {
    frame = &frames[currentFrame];
    checkVk(vkWaitForFences(
                device, 1, &frame->fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
    VkResult result = vkAcquireNextImageKHR(
        device, swapchain, UINT64_MAX, frame->imageAvailable,
        VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        createSwapchain(width, height);
        return false;
    }
    recreateAfterPresent = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        failVk("vkAcquireNextImageKHR", result);
    }
    checkVk(vkResetFences(device, 1, &frame->fence), "vkResetFences");
    checkVk(vkResetCommandBuffer(frame->commandBuffer, 0),
            "vkResetCommandBuffer");
    return true;
}

bool VulkanPresenter::submitPresentFrame(
    u32 width, u32 height, FrameResources& frame,
    u32 imageIndex, bool recreateAfterPresent) {
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
    checkVk(vkQueueSubmit(queue, 1, &submitInfo, frame.fence),
            "vkQueueSubmit");
    imageInitialized[imageIndex] = true;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
        result != VK_ERROR_OUT_OF_DATE_KHR) {
        failVk("vkQueuePresentKHR", result);
    }

    const bool presented = result != VK_ERROR_OUT_OF_DATE_KHR;
    currentFrame = (currentFrame + 1) % framesInFlight;
    if (recreateAfterPresent || result == VK_SUBOPTIMAL_KHR ||
        result == VK_ERROR_OUT_OF_DATE_KHR) {
        createSwapchain(width, height);
    }
    return presented;
}

bool VulkanPresenter::repaint() {
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
    if (!acquirePresentFrame(
            width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }
    recordRepaintCommands(*frame, imageIndex);
    return submitPresentFrame(
        width, height, *frame, imageIndex, recreateAfterPresent);
}

bool VulkanPresenter::present(
    const CharVdev& charVdev, const Frame& sourceFrame, bool delta) {
    const u32 width = charVdev.pixelWidth();
    const u32 height = charVdev.pixelHeight();
    if (charVdev.cellData() == nullptr ||
        charVdev.cellCount() == 0 || width == 0 || height == 0) {
        return false;
    }

    if (swapchain == VK_NULL_HANDLE ||
        renderExtent.width != width || renderExtent.height != height) {
        createSwapchain(width, height);
    }
    if (swapchain == VK_NULL_HANDLE) {
        return false;
    }

    delta = delta && outputInitialized && previousStateValid;

    FrameResources* frame = nullptr;
    u32 imageIndex = 0;
    bool recreateAfterPresent = false;
    if (!acquirePresentFrame(
            width, height, frame, imageIndex, recreateAfterPresent)) {
        return false;
    }

    std::vector<GpuCell> gpuCells;
    gpuCells.reserve(charVdev.cellCount());
    std::vector<u32> graphemeData = {0};
    for (size_t index = 0; index < charVdev.cellCount(); ++index) {
        const TerminalCell& cell = charVdev.cellData()[index];
        u32 graphemeIndex = 0;
        if (cell.grapheme) {
            const auto& grapheme = sourceFrame.getGrapheme(cell.grapheme);
            if (!grapheme.empty()) {
                graphemeIndex = graphemeData.size();
                graphemeData.push_back(grapheme.size());
                graphemeData.insert(
                    graphemeData.end(), grapheme.begin(), grapheme.end());
            }
        }
        gpuCells.push_back({
            cell.uc_pt,
            packCellAttributes(cell),
            packColor(cell.fg),
            packColor(cell.bg),
            packColor(cell.underline_color),
            cell.hyperlink,
            graphemeIndex,
            cell.fg_index,
            cell.bg_index,
            cell.underline_index,
            cell.semantic,
            cell.line_attr,
        });
    }

    const size_t cellBytes =
        gpuCells.size() * sizeof(GpuCell);
    ensureCellBuffer(*frame, cellBytes);
    std::memcpy(frame->cells, gpuCells.data(), cellBytes);
    const size_t graphemeBytes = graphemeData.size() * sizeof(u32);
    ensureGraphemeBuffer(*frame, graphemeBytes);
    std::memcpy(frame->graphemes, graphemeData.data(), graphemeBytes);

    recordCommands(*frame, imageIndex, charVdev, sourceFrame, delta);
    outputInitialized = true;
    previousCursor = charVdev.getCursor();
    previousSelection = charVdev.getSelection();
    previousStateValid = true;
    return submitPresentFrame(
        width, height, *frame, imageIndex, recreateAfterPresent);
}
