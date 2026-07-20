/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once
#include <std/sys/types.h>

#include "char_vdev.h"
#include "font_pack.h"

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
    bool repaint();
    static u32 packCellAttributes(const TerminalCell& cell);

private:
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
    static_assert(sizeof(PushConstants) == 112,
                  "Vulkan push constant layout mismatch");

    static constexpr u32 framesInFlight = 2;

    GLFWwindow* window = nullptr;
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
    void createFontResources(Fontpack* fontpk);
    void createDescriptors();
    void createPipeline();
    void createSwapchain(u32 width, u32 height);
    void destroySwapchain();
    void createOutputImage(u32 width, u32 height);
    void ensureCellBuffer(FrameResources& frame, size_t bytes);
    void ensureGraphemeBuffer(FrameResources& frame, size_t bytes);

    ImageResource createImage(
        u32 width, u32 height, u32 layers,
        VkFormat format, VkImageUsageFlags usage,
        bool arrayView = false);
    void destroyImage(ImageResource& image);
    void uploadImage(const ImageResource& image, const void* data,
                     size_t bytes);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory) const;
    u32 findMemoryType(u32 allowed,
                            VkMemoryPropertyFlags properties) const;
    void updateStaticDescriptors();
    void updateOutputDescriptors();
    void updateCellDescriptor(FrameResources& frame);
    void updateGraphemeDescriptor(FrameResources& frame);
    void recordCommands(FrameResources& frame, u32 imageIndex,
                        const CharVdev& charVdev,
                        const Frame& sourceFrame, bool delta);
    void recordRepaintCommands(
        FrameResources& frame, u32 imageIndex);
    bool acquirePresentFrame(
        u32 width, u32 height, FrameResources*& frame,
        u32& imageIndex, bool& recreateAfterPresent);
    bool submitPresentFrame(
        u32 width, u32 height, FrameResources& frame,
        u32 imageIndex, bool recreateAfterPresent);

    static std::vector<u8> makeAtlasMap(const Font& font);
    static u32 packColor(const Color& color);
    static bool sameSelection(const Rect& lhs, const Rect& rhs);
};
