/* This file is part of Zutty.
 * Copyright (C) 2026 Zutty contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "vkpresenter.h"

#include "log.h"
#include "options.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace zutty
{
   namespace
   {
      [[noreturn]] void
      failVk (const char* operation, VkResult result)
      {
         throw std::runtime_error (
            std::string (operation) + " failed (VkResult " +
            std::to_string (static_cast <int> (result)) + ")");
      }

      void
      checkVk (VkResult result, const char* operation)
      {
         if (result != VK_SUCCESS)
            failVk (operation, result);
      }

      bool
      deviceHasSwapchain (VkPhysicalDevice physicalDevice)
      {
         uint32_t count = 0;
         if (vkEnumerateDeviceExtensionProperties (
                physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS)
            return false;

         std::vector <VkExtensionProperties> extensions (count);
         if (vkEnumerateDeviceExtensionProperties (
                physicalDevice, nullptr, &count, extensions.data ()) !=
             VK_SUCCESS)
            return false;

         return std::any_of (
            extensions.begin (), extensions.end (),
            [] (const VkExtensionProperties& extension)
            {
               return std::strcmp (extension.extensionName,
                                   VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
      }

      VkCompositeAlphaFlagBitsKHR
      selectCompositeAlpha (VkCompositeAlphaFlagsKHR supported)
      {
         const VkCompositeAlphaFlagBitsKHR choices [] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
         };
         for (const auto choice: choices)
            if (supported & choice)
               return choice;
         throw std::runtime_error ("Vulkan surface has no composite alpha mode");
      }
   }

   VulkanPresenter::VulkanPresenter (SDL_Window* window_)
      : window (window_)
   {
      createInstance ();
      if (!SDL_Vulkan_CreateSurface (window, instance, nullptr, &surface))
         throw std::runtime_error (
            std::string ("SDL_Vulkan_CreateSurface failed: ") +
            SDL_GetError ());
      selectPhysicalDevice ();
      createDevice ();
      createCommandResources ();
   }

   VulkanPresenter::~VulkanPresenter ()
   {
      if (device != VK_NULL_HANDLE)
         vkDeviceWaitIdle (device);

      destroySwapchain ();
      for (auto& frame: frames)
      {
         if (frame.mapped != nullptr)
            vkUnmapMemory (device, frame.stagingMemory);
         if (frame.stagingBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer (device, frame.stagingBuffer, nullptr);
         if (frame.stagingMemory != VK_NULL_HANDLE)
            vkFreeMemory (device, frame.stagingMemory, nullptr);
         if (frame.imageAvailable != VK_NULL_HANDLE)
            vkDestroySemaphore (device, frame.imageAvailable, nullptr);
         if (frame.renderFinished != VK_NULL_HANDLE)
            vkDestroySemaphore (device, frame.renderFinished, nullptr);
         if (frame.fence != VK_NULL_HANDLE)
            vkDestroyFence (device, frame.fence, nullptr);
      }

      if (commandPool != VK_NULL_HANDLE)
         vkDestroyCommandPool (device, commandPool, nullptr);
      if (device != VK_NULL_HANDLE)
         vkDestroyDevice (device, nullptr);
      if (surface != VK_NULL_HANDLE)
         SDL_Vulkan_DestroySurface (instance, surface, nullptr);
      if (instance != VK_NULL_HANDLE)
         vkDestroyInstance (instance, nullptr);
   }

   void
   VulkanPresenter::createInstance ()
   {
      Uint32 extensionCount = 0;
      const char* const* extensions =
         SDL_Vulkan_GetInstanceExtensions (&extensionCount);
      if (extensions == nullptr)
         throw std::runtime_error (
            std::string ("SDL_Vulkan_GetInstanceExtensions failed: ") +
            SDL_GetError ());

      VkApplicationInfo appInfo {};
      appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      appInfo.pApplicationName = "zutty";
      appInfo.applicationVersion = VK_MAKE_VERSION (0, 16, 0);
      appInfo.pEngineName = "zutty";
      appInfo.engineVersion = VK_MAKE_VERSION (0, 1, 0);
      appInfo.apiVersion = VK_API_VERSION_1_1;

      VkInstanceCreateInfo createInfo {};
      createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      createInfo.pApplicationInfo = &appInfo;
      createInfo.enabledExtensionCount = extensionCount;
      createInfo.ppEnabledExtensionNames = extensions;
      checkVk (vkCreateInstance (&createInfo, nullptr, &instance),
               "vkCreateInstance");
   }

   void
   VulkanPresenter::selectPhysicalDevice ()
   {
      uint32_t deviceCount = 0;
      checkVk (vkEnumeratePhysicalDevices (instance, &deviceCount, nullptr),
               "vkEnumeratePhysicalDevices");
      if (deviceCount == 0)
         throw std::runtime_error ("No Vulkan physical devices found");

      std::vector <VkPhysicalDevice> devices (deviceCount);
      checkVk (vkEnumeratePhysicalDevices (
                  instance, &deviceCount, devices.data ()),
               "vkEnumeratePhysicalDevices");

      int bestScore = -1;
      VkPhysicalDeviceProperties bestProperties {};
      for (const auto candidate: devices)
      {
         if (!deviceHasSwapchain (candidate))
            continue;

         uint32_t familyCount = 0;
         vkGetPhysicalDeviceQueueFamilyProperties (
            candidate, &familyCount, nullptr);
         std::vector <VkQueueFamilyProperties> families (familyCount);
         vkGetPhysicalDeviceQueueFamilyProperties (
            candidate, &familyCount, families.data ());

         for (uint32_t family = 0; family < familyCount; ++family)
         {
            VkBool32 presentSupported = VK_FALSE;
            checkVk (vkGetPhysicalDeviceSurfaceSupportKHR (
                        candidate, family, surface, &presentSupported),
                     "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (!(families [family].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
                !presentSupported)
               continue;

            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties (candidate, &properties);
            const int score =
               properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
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

      if (physicalDevice == VK_NULL_HANDLE)
         throw std::runtime_error (
            "No Vulkan device can present to the SDL Wayland surface");

      if (opts.vulkanInfo)
      {
         std::cout << "Vulkan device: " << bestProperties.deviceName << '\n'
                   << "Vulkan API: "
                   << VK_VERSION_MAJOR (bestProperties.apiVersion) << '.'
                   << VK_VERSION_MINOR (bestProperties.apiVersion) << '.'
                   << VK_VERSION_PATCH (bestProperties.apiVersion) << '\n';
      }
   }

   void
   VulkanPresenter::createDevice ()
   {
      constexpr float priority = 1.0f;
      VkDeviceQueueCreateInfo queueInfo {};
      queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueInfo.queueFamilyIndex = queueFamily;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;

      const char* extensions [] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
      VkDeviceCreateInfo createInfo {};
      createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      createInfo.queueCreateInfoCount = 1;
      createInfo.pQueueCreateInfos = &queueInfo;
      createInfo.enabledExtensionCount = 1;
      createInfo.ppEnabledExtensionNames = extensions;
      checkVk (vkCreateDevice (
                  physicalDevice, &createInfo, nullptr, &device),
               "vkCreateDevice");
      vkGetDeviceQueue (device, queueFamily, 0, &queue);
   }

   void
   VulkanPresenter::createCommandResources ()
   {
      VkCommandPoolCreateInfo poolInfo {};
      poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      poolInfo.queueFamilyIndex = queueFamily;
      checkVk (vkCreateCommandPool (
                  device, &poolInfo, nullptr, &commandPool),
               "vkCreateCommandPool");

      std::array <VkCommandBuffer, framesInFlight> commandBuffers {};
      VkCommandBufferAllocateInfo allocateInfo {};
      allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocateInfo.commandPool = commandPool;
      allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocateInfo.commandBufferCount = framesInFlight;
      checkVk (vkAllocateCommandBuffers (
                  device, &allocateInfo, commandBuffers.data ()),
               "vkAllocateCommandBuffers");

      VkSemaphoreCreateInfo semaphoreInfo {};
      semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      VkFenceCreateInfo fenceInfo {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      for (uint32_t i = 0; i < framesInFlight; ++i)
      {
         frames [i].commandBuffer = commandBuffers [i];
         checkVk (vkCreateSemaphore (
                     device, &semaphoreInfo, nullptr,
                     &frames [i].imageAvailable),
                  "vkCreateSemaphore");
         checkVk (vkCreateSemaphore (
                     device, &semaphoreInfo, nullptr,
                     &frames [i].renderFinished),
                  "vkCreateSemaphore");
         checkVk (vkCreateFence (
                     device, &fenceInfo, nullptr, &frames [i].fence),
                  "vkCreateFence");
      }
   }

   void
   VulkanPresenter::destroySwapchain ()
   {
      swapchainImages.clear ();
      imageInitialized.clear ();
      if (swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
         vkDestroySwapchainKHR (device, swapchain, nullptr);
      swapchain = VK_NULL_HANDLE;
      swapchainFormat = VK_FORMAT_UNDEFINED;
      swapchainExtent = {};
   }

   void
   VulkanPresenter::createSwapchain (uint32_t width, uint32_t height)
   {
      if (width == 0 || height == 0)
         return;

      checkVk (vkDeviceWaitIdle (device), "vkDeviceWaitIdle");

      VkSurfaceCapabilitiesKHR capabilities {};
      checkVk (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (
                  physicalDevice, surface, &capabilities),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
      if (!(capabilities.supportedUsageFlags &
            VK_IMAGE_USAGE_TRANSFER_DST_BIT))
         throw std::runtime_error (
            "Vulkan Wayland surface cannot be used as a transfer target");

      uint32_t formatCount = 0;
      checkVk (vkGetPhysicalDeviceSurfaceFormatsKHR (
                  physicalDevice, surface, &formatCount, nullptr),
               "vkGetPhysicalDeviceSurfaceFormatsKHR");
      if (formatCount == 0)
         throw std::runtime_error ("Vulkan surface exposes no formats");
      std::vector <VkSurfaceFormatKHR> formats (formatCount);
      checkVk (vkGetPhysicalDeviceSurfaceFormatsKHR (
                  physicalDevice, surface, &formatCount, formats.data ()),
               "vkGetPhysicalDeviceSurfaceFormatsKHR");

      VkSurfaceFormatKHR surfaceFormat = formats.front ();
      const VkFormat preferredFormats [] = {
         VK_FORMAT_B8G8R8A8_UNORM,
         VK_FORMAT_R8G8B8A8_UNORM,
         VK_FORMAT_B8G8R8A8_SRGB,
         VK_FORMAT_R8G8B8A8_SRGB,
      };
      for (const auto preferred: preferredFormats)
      {
         const auto found = std::find_if (
            formats.begin (), formats.end (),
            [preferred] (const VkSurfaceFormatKHR& format)
            {
               return format.format == preferred;
            });
         if (found != formats.end ())
         {
            surfaceFormat = *found;
            break;
         }
      }
      if (surfaceFormat.format != VK_FORMAT_B8G8R8A8_UNORM &&
          surfaceFormat.format != VK_FORMAT_B8G8R8A8_SRGB &&
          surfaceFormat.format != VK_FORMAT_R8G8B8A8_UNORM &&
          surfaceFormat.format != VK_FORMAT_R8G8B8A8_SRGB)
         throw std::runtime_error (
            "Vulkan surface has no supported 32-bit RGBA/BGRA format");

      uint32_t presentModeCount = 0;
      checkVk (vkGetPhysicalDeviceSurfacePresentModesKHR (
                  physicalDevice, surface, &presentModeCount, nullptr),
               "vkGetPhysicalDeviceSurfacePresentModesKHR");
      std::vector <VkPresentModeKHR> presentModes (presentModeCount);
      checkVk (vkGetPhysicalDeviceSurfacePresentModesKHR (
                  physicalDevice, surface, &presentModeCount,
                  presentModes.data ()),
               "vkGetPhysicalDeviceSurfacePresentModesKHR");
      VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
      if (std::find (presentModes.begin (), presentModes.end (),
                     VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end ())
         presentMode = VK_PRESENT_MODE_MAILBOX_KHR;

      VkExtent2D extent {};
      if (capabilities.currentExtent.width !=
          std::numeric_limits <uint32_t>::max ())
         extent = capabilities.currentExtent;
      else
      {
         extent.width = std::clamp (
            width, capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
         extent.height = std::clamp (
            height, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
      }

      uint32_t imageCount = capabilities.minImageCount + 1;
      if (capabilities.maxImageCount > 0)
         imageCount = std::min (imageCount, capabilities.maxImageCount);

      VkSwapchainCreateInfoKHR createInfo {};
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
         selectCompositeAlpha (capabilities.supportedCompositeAlpha);
      createInfo.presentMode = presentMode;
      createInfo.clipped = VK_TRUE;
      createInfo.oldSwapchain = swapchain;

      VkSwapchainKHR replacement = VK_NULL_HANDLE;
      checkVk (vkCreateSwapchainKHR (
                  device, &createInfo, nullptr, &replacement),
               "vkCreateSwapchainKHR");
      if (swapchain != VK_NULL_HANDLE)
         vkDestroySwapchainKHR (device, swapchain, nullptr);
      swapchain = replacement;
      swapchainFormat = surfaceFormat.format;
      swapchainExtent = extent;

      checkVk (vkGetSwapchainImagesKHR (
                  device, swapchain, &imageCount, nullptr),
               "vkGetSwapchainImagesKHR");
      swapchainImages.resize (imageCount);
      checkVk (vkGetSwapchainImagesKHR (
                  device, swapchain, &imageCount, swapchainImages.data ()),
               "vkGetSwapchainImagesKHR");
      imageInitialized.assign (imageCount, false);

      logI << "Vulkan swapchain " << extent.width << " x " << extent.height
           << ", " << imageCount << " images" << std::endl;
   }

   uint32_t
   VulkanPresenter::findMemoryType (
      uint32_t allowed, VkMemoryPropertyFlags properties) const
   {
      VkPhysicalDeviceMemoryProperties memoryProperties {};
      vkGetPhysicalDeviceMemoryProperties (
         physicalDevice, &memoryProperties);
      for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
         if ((allowed & (1u << i)) &&
             (memoryProperties.memoryTypes [i].propertyFlags & properties) ==
                properties)
            return i;
      throw std::runtime_error ("No suitable Vulkan memory type found");
   }

   void
   VulkanPresenter::ensureStagingBuffer (
      FrameResources& frame, size_t bytes)
   {
      if (frame.capacity >= bytes)
         return;

      if (frame.mapped != nullptr)
      {
         vkUnmapMemory (device, frame.stagingMemory);
         frame.mapped = nullptr;
      }
      if (frame.stagingBuffer != VK_NULL_HANDLE)
         vkDestroyBuffer (device, frame.stagingBuffer, nullptr);
      if (frame.stagingMemory != VK_NULL_HANDLE)
         vkFreeMemory (device, frame.stagingMemory, nullptr);

      VkBufferCreateInfo bufferInfo {};
      bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferInfo.size = bytes;
      bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      checkVk (vkCreateBuffer (
                  device, &bufferInfo, nullptr, &frame.stagingBuffer),
               "vkCreateBuffer");

      VkMemoryRequirements requirements {};
      vkGetBufferMemoryRequirements (
         device, frame.stagingBuffer, &requirements);
      VkMemoryAllocateInfo allocationInfo {};
      allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocationInfo.allocationSize = requirements.size;
      allocationInfo.memoryTypeIndex = findMemoryType (
         requirements.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      checkVk (vkAllocateMemory (
                  device, &allocationInfo, nullptr, &frame.stagingMemory),
               "vkAllocateMemory");
      checkVk (vkBindBufferMemory (
                  device, frame.stagingBuffer, frame.stagingMemory, 0),
               "vkBindBufferMemory");
      checkVk (vkMapMemory (
                  device, frame.stagingMemory, 0, bytes, 0, &frame.mapped),
               "vkMapMemory");
      frame.capacity = bytes;
   }

   void
   VulkanPresenter::copyPixels (
      FrameResources& frame, const uint8_t* rgba,
      uint32_t width, uint32_t height) const
   {
      const size_t bytes = static_cast <size_t> (width) * height * 4;
      auto* destination = static_cast <uint8_t*> (frame.mapped);
      if (swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM ||
          swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB)
      {
         std::memcpy (destination, rgba, bytes);
         return;
      }

      for (size_t offset = 0; offset < bytes; offset += 4)
      {
         destination [offset + 0] = rgba [offset + 2];
         destination [offset + 1] = rgba [offset + 1];
         destination [offset + 2] = rgba [offset + 0];
         destination [offset + 3] = rgba [offset + 3];
      }
   }

   void
   VulkanPresenter::present (
      const uint8_t* rgba, uint32_t width, uint32_t height)
   {
      if (rgba == nullptr || width == 0 || height == 0)
         return;

      if (swapchain == VK_NULL_HANDLE ||
          swapchainExtent.width != width ||
          swapchainExtent.height != height)
         createSwapchain (width, height);
      if (swapchain == VK_NULL_HANDLE)
         return;

      FrameResources& frame = frames [currentFrame];
      checkVk (vkWaitForFences (
                  device, 1, &frame.fence, VK_TRUE, UINT64_MAX),
               "vkWaitForFences");

      uint32_t imageIndex = 0;
      VkResult result = vkAcquireNextImageKHR (
         device, swapchain, UINT64_MAX, frame.imageAvailable,
         VK_NULL_HANDLE, &imageIndex);
      if (result == VK_ERROR_OUT_OF_DATE_KHR)
      {
         createSwapchain (width, height);
         return;
      }
      const bool recreateAfterPresent = result == VK_SUBOPTIMAL_KHR;
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
         failVk ("vkAcquireNextImageKHR", result);

      const size_t bytes = static_cast <size_t> (width) * height * 4;
      ensureStagingBuffer (frame, bytes);
      copyPixels (frame, rgba, width, height);

      checkVk (vkResetFences (device, 1, &frame.fence), "vkResetFences");
      checkVk (vkResetCommandBuffer (frame.commandBuffer, 0),
               "vkResetCommandBuffer");

      VkCommandBufferBeginInfo beginInfo {};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      checkVk (vkBeginCommandBuffer (frame.commandBuffer, &beginInfo),
               "vkBeginCommandBuffer");

      VkImageMemoryBarrier toTransfer {};
      toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      toTransfer.srcAccessMask = imageInitialized [imageIndex]
         ? VK_ACCESS_MEMORY_READ_BIT
         : 0;
      toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      toTransfer.oldLayout = imageInitialized [imageIndex]
         ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
         : VK_IMAGE_LAYOUT_UNDEFINED;
      toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toTransfer.image = swapchainImages [imageIndex];
      toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      toTransfer.subresourceRange.levelCount = 1;
      toTransfer.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier (
         frame.commandBuffer,
         imageInitialized [imageIndex]
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
         0, nullptr, 0, nullptr, 1, &toTransfer);

      VkBufferImageCopy copy {};
      copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      copy.imageSubresource.layerCount = 1;
      copy.imageExtent = {width, height, 1};
      vkCmdCopyBufferToImage (
         frame.commandBuffer, frame.stagingBuffer,
         swapchainImages [imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         1, &copy);

      VkImageMemoryBarrier toPresent {};
      toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toPresent.image = swapchainImages [imageIndex];
      toPresent.subresourceRange = toTransfer.subresourceRange;
      vkCmdPipelineBarrier (
         frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
         0, nullptr, 0, nullptr, 1, &toPresent);
      checkVk (vkEndCommandBuffer (frame.commandBuffer),
               "vkEndCommandBuffer");

      const VkPipelineStageFlags waitStage =
         VK_PIPELINE_STAGE_TRANSFER_BIT;
      VkSubmitInfo submitInfo {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = &frame.imageAvailable;
      submitInfo.pWaitDstStageMask = &waitStage;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &frame.commandBuffer;
      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = &frame.renderFinished;
      checkVk (vkQueueSubmit (queue, 1, &submitInfo, frame.fence),
               "vkQueueSubmit");
      imageInitialized [imageIndex] = true;

      VkPresentInfoKHR presentInfo {};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &frame.renderFinished;
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = &swapchain;
      presentInfo.pImageIndices = &imageIndex;
      result = vkQueuePresentKHR (queue, &presentInfo);
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
          result != VK_ERROR_OUT_OF_DATE_KHR)
         failVk ("vkQueuePresentKHR", result);

      currentFrame = (currentFrame + 1) % framesInFlight;
      if (recreateAfterPresent || result == VK_SUBOPTIMAL_KHR ||
          result == VK_ERROR_OUT_OF_DATE_KHR)
         createSwapchain (width, height);
   }

} // namespace zutty
