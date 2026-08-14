#include "elf_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>

typedef struct {
    void *base;
    size_t size;
} ShAllocationHeader;

static void *VKAPI_PTR sh_allocate(void *user_data, size_t size, size_t alignment,
                                   VkSystemAllocationScope scope) {
    (void)user_data;
    (void)scope;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    size_t total = size + alignment - 1 + sizeof(ShAllocationHeader);
    void *base = malloc(total);
    if (!base) return NULL;
    uintptr_t address = ((uintptr_t)base + sizeof(ShAllocationHeader) + alignment - 1) &
                        ~(uintptr_t)(alignment - 1);
    ShAllocationHeader *header = (ShAllocationHeader *)address - 1;
    header->base = base;
    header->size = size;
    return (void *)address;
}

static void VKAPI_PTR sh_free(void *user_data, void *memory) {
    (void)user_data;
    if (!memory) return;
    ShAllocationHeader *header = (ShAllocationHeader *)memory - 1;
    free(header->base);
}

static void *VKAPI_PTR sh_reallocate(void *user_data, void *original, size_t size,
                                     size_t alignment,
                                     VkSystemAllocationScope scope) {
    if (!original) return sh_allocate(user_data, size, alignment, scope);
    if (!size) {
        sh_free(user_data, original);
        return NULL;
    }
    ShAllocationHeader *header = (ShAllocationHeader *)original - 1;
    void *replacement = sh_allocate(user_data, size, alignment, scope);
    if (!replacement) return NULL;
    size_t copy_size = header->size < size ? header->size : size;
    memcpy(replacement, original, copy_size);
    sh_free(user_data, original);
    return replacement;
}

static const VkAllocationCallbacks sh_allocator = {
    .pfnAllocation = sh_allocate,
    .pfnReallocation = sh_reallocate,
    .pfnFree = sh_free,
};

static void fail(const char *operation, VkResult result) {
    fprintf(stderr, "%s failed: VkResult %d\n", operation, result);
    exit(1);
}

static void missing(const char *name) {
    fprintf(stderr, "ICD did not expose %s\n", name);
    exit(1);
}

int main(int argument_count, char **arguments) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *path = argument_count > 1
        ? arguments[1]
        : "../arch/root/usr/lib/libvulkan_radeon.so";
    ShElfImage *loader_image = sh_elf_load(path);
    if (!loader_image) {
        fprintf(stderr, "load failed: %s\n", sh_elf_error());
        return 1;
    }

    PFN_vk_icdNegotiateLoaderICDInterfaceVersion negotiate =
        (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)sh_elf_symbol(
            loader_image, "vk_icdNegotiateLoaderICDInterfaceVersion");
    PFN_vk_icdGetInstanceProcAddr get_instance_proc_addr =
        (PFN_vk_icdGetInstanceProcAddr)sh_elf_symbol(
            loader_image, "vk_icdGetInstanceProcAddr");
    if (!negotiate || !get_instance_proc_addr) {
        fprintf(stderr, "ICD entry points are missing\n");
        return 1;
    }

    uint32_t interface_version = 7;
    VkResult result = negotiate(&interface_version);
    if (result != VK_SUCCESS) fail("vk_icdNegotiateLoaderICDInterfaceVersion", result);
    printf("ICD loader interface: %u\n", interface_version);

    PFN_vkCreateInstance create_instance =
        (PFN_vkCreateInstance)get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create_instance) {
        fprintf(stderr, "ICD did not expose vkCreateInstance\n");
        return 1;
    }

    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "shitty-glibc-bridge",
        .applicationVersion = 1,
        .pEngineName = "none",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance = VK_NULL_HANDLE;
    result = create_instance(&create_info, &sh_allocator, &instance);
    if (result != VK_SUCCESS) fail("vkCreateInstance", result);

    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices =
        (PFN_vkEnumeratePhysicalDevices)get_instance_proc_addr(
            instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties =
        (PFN_vkGetPhysicalDeviceProperties)get_instance_proc_addr(
            instance, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy_instance =
        (PFN_vkDestroyInstance)get_instance_proc_addr(instance, "vkDestroyInstance");
    if (!enumerate_physical_devices || !get_physical_device_properties || !destroy_instance) {
        fprintf(stderr, "ICD instance dispatch is incomplete\n");
        return 1;
    }

    uint32_t physical_device_count = 0;
    result = enumerate_physical_devices(instance, &physical_device_count, NULL);
    if (result != VK_SUCCESS) fail("vkEnumeratePhysicalDevices(count)", result);
    if (physical_device_count == 0) {
        fprintf(stderr, "RADV loaded, but no physical devices were found\n");
        destroy_instance(instance, &sh_allocator);
        return 1;
    }
    VkPhysicalDevice *physical_devices =
        calloc(physical_device_count, sizeof(*physical_devices));
    if (!physical_devices) return 1;
    result = enumerate_physical_devices(instance, &physical_device_count, physical_devices);
    if (result != VK_SUCCESS) fail("vkEnumeratePhysicalDevices(list)", result);
    for (uint32_t index = 0; index < physical_device_count; ++index) {
        VkPhysicalDeviceProperties properties;
        get_physical_device_properties(physical_devices[index], &properties);
        printf("physical device %u: %s (api %u.%u.%u)\n", index,
               properties.deviceName,
               VK_API_VERSION_MAJOR(properties.apiVersion),
               VK_API_VERSION_MINOR(properties.apiVersion),
               VK_API_VERSION_PATCH(properties.apiVersion));
    }

    VkPhysicalDevice physical_device = physical_devices[0];
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_family_properties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)get_instance_proc_addr(
            instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)get_instance_proc_addr(
            instance, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkCreateDevice create_device =
        (PFN_vkCreateDevice)get_instance_proc_addr(instance, "vkCreateDevice");
    PFN_vkGetDeviceProcAddr get_device_proc_addr =
        (PFN_vkGetDeviceProcAddr)get_instance_proc_addr(instance, "vkGetDeviceProcAddr");
    if (!get_queue_family_properties) missing("vkGetPhysicalDeviceQueueFamilyProperties");
    if (!get_memory_properties) missing("vkGetPhysicalDeviceMemoryProperties");
    if (!create_device) missing("vkCreateDevice");
    if (!get_device_proc_addr) missing("vkGetDeviceProcAddr");

    uint32_t queue_family_count = 0;
    get_queue_family_properties(physical_device, &queue_family_count, NULL);
    VkQueueFamilyProperties *queue_families =
        calloc(queue_family_count, sizeof(*queue_families));
    if (!queue_families) return 1;
    get_queue_family_properties(physical_device, &queue_family_count, queue_families);
    uint32_t queue_family = UINT32_MAX;
    for (uint32_t index = 0; index < queue_family_count; ++index) {
        if (queue_families[index].queueFlags &
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) {
            queue_family = index;
            break;
        }
    }
    free(queue_families);
    if (queue_family == UINT32_MAX) {
        fprintf(stderr, "physical device has no transfer-capable queue\n");
        return 1;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
    };
    VkDevice device = VK_NULL_HANDLE;
    result = create_device(physical_device, &device_create_info, &sh_allocator, &device);
    if (result != VK_SUCCESS) fail("vkCreateDevice", result);

#define LOAD_DEVICE(type, variable, name)                                      \
    type variable = (type)get_device_proc_addr(device, name);                  \
    if (!variable) missing(name)
    LOAD_DEVICE(PFN_vkDestroyDevice, destroy_device, "vkDestroyDevice");
    LOAD_DEVICE(PFN_vkGetDeviceQueue, get_device_queue, "vkGetDeviceQueue");
    LOAD_DEVICE(PFN_vkCreateImage, create_image, "vkCreateImage");
    LOAD_DEVICE(PFN_vkDestroyImage, destroy_image, "vkDestroyImage");
    LOAD_DEVICE(PFN_vkGetImageMemoryRequirements, get_image_memory_requirements,
                "vkGetImageMemoryRequirements");
    LOAD_DEVICE(PFN_vkAllocateMemory, allocate_memory, "vkAllocateMemory");
    LOAD_DEVICE(PFN_vkFreeMemory, free_memory, "vkFreeMemory");
    LOAD_DEVICE(PFN_vkBindImageMemory, bind_image_memory, "vkBindImageMemory");
    LOAD_DEVICE(PFN_vkGetImageSubresourceLayout, get_image_subresource_layout,
                "vkGetImageSubresourceLayout");
    LOAD_DEVICE(PFN_vkMapMemory, map_memory, "vkMapMemory");
    LOAD_DEVICE(PFN_vkUnmapMemory, unmap_memory, "vkUnmapMemory");
    LOAD_DEVICE(PFN_vkCreateCommandPool, create_command_pool, "vkCreateCommandPool");
    LOAD_DEVICE(PFN_vkDestroyCommandPool, destroy_command_pool, "vkDestroyCommandPool");
    LOAD_DEVICE(PFN_vkAllocateCommandBuffers, allocate_command_buffers,
                "vkAllocateCommandBuffers");
    LOAD_DEVICE(PFN_vkBeginCommandBuffer, begin_command_buffer, "vkBeginCommandBuffer");
    LOAD_DEVICE(PFN_vkEndCommandBuffer, end_command_buffer, "vkEndCommandBuffer");
    LOAD_DEVICE(PFN_vkCmdPipelineBarrier, cmd_pipeline_barrier, "vkCmdPipelineBarrier");
    LOAD_DEVICE(PFN_vkCmdClearColorImage, cmd_clear_color_image,
                "vkCmdClearColorImage");
    LOAD_DEVICE(PFN_vkQueueSubmit, queue_submit, "vkQueueSubmit");
    LOAD_DEVICE(PFN_vkQueueWaitIdle, queue_wait_idle, "vkQueueWaitIdle");
#undef LOAD_DEVICE

    enum { image_width = 32, image_height = 32 };
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {image_width, image_height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    result = create_image(device, &image_create_info, &sh_allocator, &image);
    if (result != VK_SUCCESS) fail("vkCreateImage", result);

    VkMemoryRequirements memory_requirements;
    get_image_memory_requirements(device, image, &memory_requirements);
    VkPhysicalDeviceMemoryProperties memory_properties;
    get_memory_properties(physical_device, &memory_properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        VkMemoryPropertyFlags wanted =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((memory_requirements.memoryTypeBits & (1u << index)) &&
            (memory_properties.memoryTypes[index].propertyFlags & wanted) == wanted) {
            memory_type = index;
            break;
        }
    }
    if (memory_type == UINT32_MAX) {
        fprintf(stderr, "linear image has no host-visible coherent memory type\n");
        return 1;
    }
    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = allocate_memory(device, &allocation_info, &sh_allocator, &memory);
    if (result != VK_SUCCESS) fail("vkAllocateMemory", result);
    result = bind_image_memory(device, image, memory, 0);
    if (result != VK_SUCCESS) fail("vkBindImageMemory", result);

    VkCommandPoolCreateInfo pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_family,
    };
    VkCommandPool command_pool = VK_NULL_HANDLE;
    result = create_command_pool(device, &pool_create_info, &sh_allocator, &command_pool);
    if (result != VK_SUCCESS) fail("vkCreateCommandPool", result);
    VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = allocate_command_buffers(device, &command_buffer_allocate_info,
                                      &command_buffer);
    if (result != VK_SUCCESS) fail("vkAllocateCommandBuffers", result);
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    result = begin_command_buffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) fail("vkBeginCommandBuffer", result);

    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1, &to_transfer);
    VkClearColorValue clear_color = {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}};
    cmd_clear_color_image(command_buffer, image, VK_IMAGE_LAYOUT_GENERAL,
                          &clear_color, 1, &range);
    VkImageMemoryBarrier to_host = to_transfer;
    to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    to_host.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL,
                         1, &to_host);
    result = end_command_buffer(command_buffer);
    if (result != VK_SUCCESS) fail("vkEndCommandBuffer", result);

    VkQueue queue = VK_NULL_HANDLE;
    get_device_queue(device, queue_family, 0, &queue);
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    result = queue_submit(queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) fail("vkQueueSubmit", result);
    result = queue_wait_idle(queue);
    if (result != VK_SUCCESS) fail("vkQueueWaitIdle", result);

    VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0,
    };
    VkSubresourceLayout layout;
    get_image_subresource_layout(device, image, &subresource, &layout);
    void *mapped = NULL;
    result = map_memory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) fail("vkMapMemory", result);
    uint64_t checksum = UINT64_C(1469598103934665603);
    unsigned char first_pixel[4] = {0};
    for (uint32_t y = 0; y < image_height; ++y) {
        unsigned char *row = (unsigned char *)mapped + layout.offset + y * layout.rowPitch;
        if (y == 0) {
            for (size_t index = 0; index < sizeof(first_pixel); ++index)
                first_pixel[index] = row[index];
        }
        for (uint32_t x = 0; x < image_width * 4; ++x) {
            checksum ^= row[x];
            checksum *= UINT64_C(1099511628211);
        }
    }
    printf("clear readback: rgba=%u,%u,%u,%u checksum=%016llx\n",
           first_pixel[0], first_pixel[1], first_pixel[2], first_pixel[3],
           (unsigned long long)checksum);
    unmap_memory(device, memory);

    destroy_command_pool(device, command_pool, &sh_allocator);
    destroy_image(device, image, &sh_allocator);
    free_memory(device, memory, &sh_allocator);
    destroy_device(device, &sh_allocator);
    free(physical_devices);
    destroy_instance(instance, &sh_allocator);
    return 0;
}
