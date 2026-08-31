#ifndef VULKAN_UTILS_H
#define VULKAN_UTILS_H

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>

// Validation layer names
extern const char* const VALIDATION_LAYERS[];
extern const uint32_t VALIDATION_LAYER_COUNT;

// Device extensions
extern const char* const DEVICE_EXTENSIONS[];
extern const uint32_t DEVICE_EXTENSION_COUNT;

// Instance extensions
extern const char* const INSTANCE_EXTENSIONS[];
extern const uint32_t INSTANCE_EXTENSION_COUNT;

// Utility functions for Vulkan operations
bool vulkan_utils_check_validation_layer_support(void);
bool vulkan_utils_check_device_extension_support(VkPhysicalDevice device);
bool vulkan_utils_is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface);

// Queue family management
typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
    bool graphics_family_has_value;
    bool present_family_has_value;
} QueueFamilyIndices;

QueueFamilyIndices vulkan_utils_find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// Swap chain support details
typedef struct {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats;
    uint32_t format_count;
    VkPresentModeKHR *present_modes;
    uint32_t present_mode_count;
} SwapChainSupportDetails;

SwapChainSupportDetails vulkan_utils_query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface);
void vulkan_utils_free_swap_chain_support_details(SwapChainSupportDetails *details);

// Buffer and memory management
bool vulkan_utils_create_buffer(VkDevice device, VkPhysicalDevice physical_device,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkBuffer *buffer, VkDeviceMemory *buffer_memory);

void vulkan_utils_destroy_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

bool vulkan_utils_copy_buffer(VkDevice device, VkCommandPool command_pool, VkQueue graphics_queue,
                             VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size);

// Image management
bool vulkan_utils_create_image(VkDevice device, VkPhysicalDevice physical_device,
                              uint32_t width, uint32_t height, VkFormat format,
                              VkImageTiling tiling, VkImageUsageFlags usage,
                              VkMemoryPropertyFlags properties,
                              VkImage *image, VkDeviceMemory *image_memory);

void vulkan_utils_destroy_image(VkDevice device, VkImage image, VkDeviceMemory memory);

bool vulkan_utils_transition_image_layout(VkDevice device, VkCommandPool command_pool,
                                         VkQueue graphics_queue, VkImage image,
                                         VkFormat format, VkImageLayout old_layout,
                                         VkImageLayout new_layout);

bool vulkan_utils_copy_buffer_to_image(VkDevice device, VkCommandPool command_pool,
                                      VkQueue graphics_queue, VkBuffer buffer,
                                      VkImage image, uint32_t width, uint32_t height);

// Command buffer utilities
VkCommandBuffer vulkan_utils_begin_single_time_commands(VkDevice device, VkCommandPool command_pool);
void vulkan_utils_end_single_time_commands(VkDevice device, VkCommandPool command_pool,
                                          VkQueue graphics_queue, VkCommandBuffer command_buffer);

// Format and feature checking
VkFormat vulkan_utils_find_supported_format(VkPhysicalDevice physical_device,
                                           const VkFormat *candidates, uint32_t candidate_count,
                                           VkImageTiling tiling, VkFormatFeatureFlags features);

VkSampleCountFlagBits vulkan_utils_get_max_usable_sample_count(VkPhysicalDevice physical_device);

// Synchronization
bool vulkan_utils_create_semaphore(VkDevice device, VkSemaphore *semaphore);
bool vulkan_utils_create_fence(VkDevice device, VkFence *fence);

// Error checking
void vulkan_utils_check_result(VkResult result, const char *operation);

// Memory requirements helper
uint32_t vulkan_utils_find_memory_type(VkPhysicalDevice physical_device,
                                       uint32_t type_filter,
                                       VkMemoryPropertyFlags properties);

#endif // VULKAN_UTILS_H
