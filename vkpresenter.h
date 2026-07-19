/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "charvdev.h"
#include "fontpack.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct GLFWwindow;
class Frame;

class VulkanPresenter {
public:
    VulkanPresenter(GLFWwindow* window, Fontpack* fontpk);
    ~VulkanPresenter();

    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;

    bool present(const CharVdev& charVdev, const Frame& sourceFrame,
                 bool delta);

private:
    struct ImageResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layers = 0;
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
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    struct PushConstants {
        uint32_t glyphWidth;
        uint32_t glyphHeight;
        uint32_t columns;
        uint32_t rows;
        uint32_t outputWidth;
        uint32_t outputHeight;
        uint32_t border;
        uint32_t cursorColor;
        int32_t cursorX;
        int32_t cursorY;
        uint32_t cursorStyle;
        uint32_t reserved;
        int32_t selectionLeft;
        int32_t selectionTop;
        int32_t selectionRight;
        int32_t selectionBottom;
        uint32_t rectangularSelection;
        uint32_t showWraps;
        uint32_t hasDoubleWidth;
        int32_t previousCursorX;
        int32_t previousCursorY;
        uint32_t deltaFrame;
        uint32_t selectionChanged;
    };
    static_assert(sizeof(PushConstants) == 92,
                  "Vulkan push constant layout mismatch");

    static constexpr uint32_t framesInFlight = 2;

    GLFWwindow* window = nullptr;
    uint32_t glyphWidth = 0;
    uint32_t glyphHeight = 0;
    bool hasDoubleWidth = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
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
    ImageResource outputImage;
    bool outputInitialized = false;
    CharVdev::Cursor previousCursor;
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
    uint32_t currentFrame = 0;

    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    void createCommandResources();
    void createFontResources(Fontpack* fontpk);
    void createDescriptors();
    void createPipeline();
    void createSwapchain(uint32_t width, uint32_t height);
    void destroySwapchain();
    void createOutputImage(uint32_t width, uint32_t height);
    void ensureCellBuffer(FrameResources& frame, size_t bytes);
    void ensureGraphemeBuffer(FrameResources& frame, size_t bytes);

    ImageResource createImage(
        uint32_t width, uint32_t height, uint32_t layers,
        VkFormat format, VkImageUsageFlags usage,
        bool arrayView = false);
    void destroyImage(ImageResource& image);
    void uploadImage(const ImageResource& image, const void* data,
                     size_t bytes);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory) const;
    uint32_t findMemoryType(uint32_t allowed,
                            VkMemoryPropertyFlags properties) const;
    void updateStaticDescriptors();
    void updateOutputDescriptors();
    void updateCellDescriptor(FrameResources& frame);
    void updateGraphemeDescriptor(FrameResources& frame);
    void recordCommands(FrameResources& frame, uint32_t imageIndex,
                        const CharVdev& charVdev, bool delta);

    static std::vector<uint8_t> makeAtlasMap(const Font& font);
    static uint32_t packColor(const Color& color);
    static bool sameSelection(const Rect& lhs, const Rect& rhs);
};
