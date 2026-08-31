#include "vulkan_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Validation layer names
const char* const VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
const uint32_t VALIDATION_LAYER_COUNT = 1;

// Device extensions (include portability for MoltenVK)
const char* const DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset"
};
const uint32_t DEVICE_EXTENSION_COUNT = 2;

// Instance extensions are derived from SDL at runtime; keep this empty/static list unused.
const char* const INSTANCE_EXTENSIONS[] = { };
const uint32_t INSTANCE_EXTENSION_COUNT = 0;

// Check if validation layers are supported
bool vulkan_utils_check_validation_layer_support(void) {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);

    VkLayerProperties *available_layers = malloc(sizeof(VkLayerProperties) * layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

    for (uint32_t i = 0; i < VALIDATION_LAYER_COUNT; i++) {
        bool layer_found = false;

        for (uint32_t j = 0; j < layer_count; j++) {
            if (strcmp(VALIDATION_LAYERS[i], available_layers[j].layerName) == 0) {
                layer_found = true;
                break;
            }
        }

        if (!layer_found) {
            free(available_layers);
            return false;
        }
    }

    free(available_layers);
    return true;
}

// Check if device extensions are supported
bool vulkan_utils_check_device_extension_support(VkPhysicalDevice device) {
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL);

    VkExtensionProperties *available_extensions = malloc(sizeof(VkExtensionProperties) * extension_count);
    vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, available_extensions);

    for (uint32_t i = 0; i < DEVICE_EXTENSION_COUNT; i++) {
        bool extension_found = false;

        for (uint32_t j = 0; j < extension_count; j++) {
            if (strcmp(DEVICE_EXTENSIONS[i], available_extensions[j].extensionName) == 0) {
                extension_found = true;
                break;
            }
        }

        if (!extension_found) {
            free(available_extensions);
            return false;
        }
    }

    free(available_extensions);
    return true;
}

// Check if device is suitable for our needs
bool vulkan_utils_is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);

    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    QueueFamilyIndices indices = vulkan_utils_find_queue_families(device, surface);

    bool extensions_supported = vulkan_utils_check_device_extension_support(device);

    bool swap_chain_adequate = false;
    if (extensions_supported) {
        SwapChainSupportDetails swap_chain_support = vulkan_utils_query_swap_chain_support(device, surface);
        swap_chain_adequate = swap_chain_support.format_count > 0 && swap_chain_support.present_mode_count > 0;
        vulkan_utils_free_swap_chain_support_details(&swap_chain_support);
    }

    return indices.graphics_family_has_value &&
           indices.present_family_has_value &&
           extensions_supported &&
           swap_chain_adequate &&
           device_features.samplerAnisotropy;
}

// Find queue families
QueueFamilyIndices vulkan_utils_find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = {0};

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

    VkQueueFamilyProperties *queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
            indices.graphics_family_has_value = true;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

        if (present_support) {
            indices.present_family = i;
            indices.present_family_has_value = true;
        }

        if (indices.graphics_family_has_value && indices.present_family_has_value) {
            break;
        }
    }

    free(queue_families);
    return indices;
}

// Query swap chain support
SwapChainSupportDetails vulkan_utils_query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details = {0};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL);

    if (format_count != 0) {
        details.formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats);
        details.format_count = format_count;
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL);

    if (present_mode_count != 0) {
        details.present_modes = malloc(sizeof(VkPresentModeKHR) * present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes);
        details.present_mode_count = present_mode_count;
    }

    return details;
}

// Free swap chain support details
void vulkan_utils_free_swap_chain_support_details(SwapChainSupportDetails *details) {
    if (details->formats) {
        free(details->formats);
        details->formats = NULL;
    }
    if (details->present_modes) {
        free(details->present_modes);
        details->present_modes = NULL;
    }
    details->format_count = 0;
    details->present_mode_count = 0;
}

// Create buffer
bool vulkan_utils_create_buffer(VkDevice device, VkPhysicalDevice physical_device,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(device, &buffer_info, NULL, buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = vulkan_utils_find_memory_type(physical_device, mem_requirements.memoryTypeBits, properties)
    };

    if (vkAllocateMemory(device, &alloc_info, NULL, buffer_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, *buffer, NULL);
        return false;
    }

    vkBindBufferMemory(device, *buffer, *buffer_memory, 0);
    return true;
}

// Destroy buffer
void vulkan_utils_destroy_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, NULL);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, NULL);
    }
}

// Copy buffer
bool vulkan_utils_copy_buffer(VkDevice device, VkCommandPool command_pool, VkQueue graphics_queue,
                             VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) {
    VkCommandBuffer command_buffer = vulkan_utils_begin_single_time_commands(device, command_pool);

    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };

    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

    vulkan_utils_end_single_time_commands(device, command_pool, graphics_queue, command_buffer);
    return true;
}

// Create image
bool vulkan_utils_create_image(VkDevice device, VkPhysicalDevice physical_device,
                              uint32_t width, uint32_t height, VkFormat format,
                              VkImageTiling tiling, VkImageUsageFlags usage,
                              VkMemoryPropertyFlags properties,
                              VkImage *image, VkDeviceMemory *image_memory) {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .samples = VK_SAMPLE_COUNT_1_BIT
    };

    if (vkCreateImage(device, &image_info, NULL, image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, *image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = vulkan_utils_find_memory_type(physical_device, mem_requirements.memoryTypeBits, properties)
    };

    if (vkAllocateMemory(device, &alloc_info, NULL, image_memory) != VK_SUCCESS) {
        vkDestroyImage(device, *image, NULL);
        return false;
    }

    vkBindImageMemory(device, *image, *image_memory, 0);
    return true;
}

// Destroy image
void vulkan_utils_destroy_image(VkDevice device, VkImage image, VkDeviceMemory memory) {
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, NULL);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, NULL);
    }
}

// Transition image layout
bool vulkan_utils_transition_image_layout(VkDevice device, VkCommandPool command_pool,
                                         VkQueue graphics_queue, VkImage image,
                                         VkFormat format, VkImageLayout old_layout,
                                         VkImageLayout new_layout) {
    VkCommandBuffer command_buffer = vulkan_utils_begin_single_time_commands(device, command_pool);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1
    };

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        return false;
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, NULL, 0, NULL, 1, &barrier);

    vulkan_utils_end_single_time_commands(device, command_pool, graphics_queue, command_buffer);
    return true;
}

// Copy buffer to image
bool vulkan_utils_copy_buffer_to_image(VkDevice device, VkCommandPool command_pool,
                                      VkQueue graphics_queue, VkBuffer buffer,
                                      VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer command_buffer = vulkan_utils_begin_single_time_commands(device, command_pool);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vulkan_utils_end_single_time_commands(device, command_pool, graphics_queue, command_buffer);
    return true;
}

// Begin single time commands
VkCommandBuffer vulkan_utils_begin_single_time_commands(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = command_pool,
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    vkBeginCommandBuffer(command_buffer, &begin_info);
    return command_buffer;
}

// End single time commands
void vulkan_utils_end_single_time_commands(VkDevice device, VkCommandPool command_pool,
                                          VkQueue graphics_queue, VkCommandBuffer command_buffer) {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer
    };

    vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

// Find supported format
VkFormat vulkan_utils_find_supported_format(VkPhysicalDevice physical_device,
                                           const VkFormat *candidates, uint32_t candidate_count,
                                           VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (uint32_t i = 0; i < candidate_count; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return candidates[i];
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return candidates[i];
        }
    }

    return VK_FORMAT_UNDEFINED;
}

// Get max usable sample count
VkSampleCountFlagBits vulkan_utils_get_max_usable_sample_count(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

    VkSampleCountFlags counts = physical_device_properties.limits.framebufferColorSampleCounts &
                                physical_device_properties.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;

    return VK_SAMPLE_COUNT_1_BIT;
}

// Create semaphore
bool vulkan_utils_create_semaphore(VkDevice device, VkSemaphore *semaphore) {
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    return vkCreateSemaphore(device, &semaphore_info, NULL, semaphore) == VK_SUCCESS;
}

// Create fence
bool vulkan_utils_create_fence(VkDevice device, VkFence *fence) {
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    return vkCreateFence(device, &fence_info, NULL, fence) == VK_SUCCESS;
}

// Check result
void vulkan_utils_check_result(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Vulkan error in %s: %d\n", operation, result);
    }
}

// Find memory type
uint32_t vulkan_utils_find_memory_type(VkPhysicalDevice physical_device,
                                       uint32_t type_filter,
                                       VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fprintf(stderr, "Failed to find suitable memory type\n");
    return 0;
}
