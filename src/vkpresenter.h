/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zutty
{
   // Minimal Vulkan presentation backend. The terminal is rasterized into a
   // host-visible RGBA8 buffer and copied to a Wayland swapchain image.
   class VulkanPresenter
   {
   public:
      explicit VulkanPresenter (SDL_Window* window);
      ~VulkanPresenter ();

      VulkanPresenter (const VulkanPresenter&) = delete;
      VulkanPresenter& operator= (const VulkanPresenter&) = delete;

      void present (const uint8_t* rgba, uint32_t width, uint32_t height);

   private:
      struct FrameResources
      {
         VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
         VkBuffer stagingBuffer = VK_NULL_HANDLE;
         VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
         void* mapped = nullptr;
         size_t capacity = 0;
         VkSemaphore imageAvailable = VK_NULL_HANDLE;
         VkSemaphore renderFinished = VK_NULL_HANDLE;
         VkFence fence = VK_NULL_HANDLE;
      };

      static constexpr uint32_t framesInFlight = 2;

      SDL_Window* window = nullptr;
      VkInstance instance = VK_NULL_HANDLE;
      VkSurfaceKHR surface = VK_NULL_HANDLE;
      VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
      VkDevice device = VK_NULL_HANDLE;
      uint32_t queueFamily = UINT32_MAX;
      VkQueue queue = VK_NULL_HANDLE;
      VkCommandPool commandPool = VK_NULL_HANDLE;

      VkSwapchainKHR swapchain = VK_NULL_HANDLE;
      VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
      VkExtent2D swapchainExtent {};
      std::vector <VkImage> swapchainImages;
      std::vector <bool> imageInitialized;

      std::array <FrameResources, framesInFlight> frames;
      uint32_t currentFrame = 0;

      void createInstance ();
      void selectPhysicalDevice ();
      void createDevice ();
      void createCommandResources ();
      void createSwapchain (uint32_t width, uint32_t height);
      void destroySwapchain ();
      void ensureStagingBuffer (FrameResources& frame, size_t bytes);

      uint32_t findMemoryType (uint32_t allowed,
                               VkMemoryPropertyFlags properties) const;
      void copyPixels (FrameResources& frame, const uint8_t* rgba,
                       uint32_t width, uint32_t height) const;
   };

} // namespace zutty
