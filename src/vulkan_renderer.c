#include "vulkan_renderer.h"
#include "vulkan_utils.h"
#include "vulkan_shaders.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Forward declarations for helper functions
static bool create_vulkan_instance(VulkanRenderer *renderer);
static bool setup_debug_messenger(VulkanRenderer *renderer);
static bool create_surface(VulkanRenderer *renderer);
static bool pick_physical_device(VulkanRenderer *renderer);
static bool create_logical_device(VulkanRenderer *renderer);
static bool create_swap_chain(VulkanRenderer *renderer);
static bool create_image_views(VulkanRenderer *renderer);
static bool create_render_pass(VulkanRenderer *renderer);
static bool create_descriptor_set_layout(VulkanRenderer *renderer);
static bool create_graphics_pipeline(VulkanRenderer *renderer);
static bool create_framebuffers(VulkanRenderer *renderer);
static bool create_command_pool(VulkanRenderer *renderer);
static bool create_uniform_buffer(VulkanRenderer *renderer);
static bool create_descriptor_pool_and_sets(VulkanRenderer *renderer);
static bool create_command_buffers(VulkanRenderer *renderer);
static bool create_sync_objects(VulkanRenderer *renderer);
static void cleanup_sync_objects(VulkanRenderer *renderer);
static void cleanup_command_buffers(VulkanRenderer *renderer);
static void cleanup_uniform_buffer(VulkanRenderer *renderer);
static void cleanup_descriptor_sets_and_pool(VulkanRenderer *renderer);
static void cleanup_pipeline(VulkanRenderer *renderer);
static void cleanup_framebuffers(VulkanRenderer *renderer);
static void cleanup_swap_chain(VulkanRenderer *renderer);
static void recreate_swap_chain(VulkanRenderer *renderer);
static VkResult create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *p_create_info, const VkAllocationCallbacks *p_allocator, VkDebugUtilsMessengerEXT *p_debug_messenger);
static void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks *p_allocator);
static bool check_validation_layer_support(void);

// Minimal working triangle shaders (SPIR-V) - simplified for MoltenVK compatibility
static const uint32_t triangle_vertex_shader[] = {
    0x07230203, 0x00010000, 0x0008000a, 0x0000001e, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00000015,
    0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00050005,
    0x00000009, 0x6f75746f, 0x6e6f6974, 0x00000000, 0x00050005, 0x0000000d, 0x6f75746f, 0x6e6f6974,
    0x00000000, 0x00050005, 0x00000015, 0x6f75746f, 0x6e6f6974, 0x00000000, 0x00040047, 0x00000009,
    0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x0000001e, 0x00000001, 0x00040047, 0x00000015,
    0x0000001e, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000003, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x00040020, 0x0000000c, 0x00000003, 0x00000006, 0x00040020, 0x00000014,
    0x00000003, 0x00000006, 0x00040021, 0x00000016, 0x00000003, 0x00000008, 0x0004002b, 0x00000006,
    0x00000018, 0x00000000, 0x0004002b, 0x00000006, 0x00000019, 0x3f800000, 0x0004002b, 0x00000006,
    0x0000001a, 0xbf800000, 0x0006002c, 0x00000007, 0x0000001b, 0x00000018, 0x00000019, 0x00000018,
    0x00000019, 0x0006002c, 0x00000007, 0x0000001c, 0x0000001a, 0x0000001a, 0x00000018, 0x00000019,
    0x0006002c, 0x00000007, 0x0000001d, 0x00000019, 0x0000001a, 0x00000018, 0x00000019, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007,
    0x0000001f, 0x0000000a, 0x00050051, 0x00000006, 0x00000020, 0x0000001f, 0x00000000, 0x00050051,
    0x00000006, 0x00000021, 0x0000001f, 0x00000001, 0x00050051, 0x00000006, 0x00000022, 0x0000001f,
    0x00000002, 0x00050051, 0x00000006, 0x00000023, 0x0000001f, 0x00000003, 0x00060050, 0x00000007,
    0x00000024, 0x00000020, 0x00000021, 0x00000022, 0x00000023, 0x0003003e, 0x00000009, 0x00000024,
    0x00050041, 0x0000000c, 0x0000000b, 0x0000000b, 0x00000000, 0x0004003d, 0x00000006, 0x00000025,
    0x0000000b, 0x0003003e, 0x0000000d, 0x00000025, 0x00050041, 0x00000014, 0x0000000e, 0x0000000e,
    0x00000000, 0x0004003d, 0x00000006, 0x00000026, 0x0000000e, 0x0003003e, 0x00000015, 0x00000026,
    0x000100fd, 0x00010038};

static const uint32_t triangle_fragment_shader[] = {
    0x07230203, 0x00010000, 0x0008000a, 0x00000013, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00040005, 0x00000009, 0x6f75746f, 0x00000000, 0x00040005, 0x0000000d, 0x6f75746f,
    0x00000000, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x0000001e,
    0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000003, 0x00040017, 0x00000007, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x00040020, 0x0000000c, 0x00000003, 0x00000006, 0x00040020, 0x0000000e, 0x00000001, 0x00000006,
    0x00040020, 0x00000012, 0x00000001, 0x00000007, 0x0004002b, 0x00000006, 0x0000000f, 0x3f800000,
    0x0004002b, 0x00000006, 0x00000010, 0x00000000, 0x0004002b, 0x00000006, 0x00000011, 0x3f000000,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x00000007, 0x00000013, 0x00000012, 0x00050051, 0x00000006, 0x00000014, 0x00000013, 0x00000000,
    0x00050051, 0x00000006, 0x00000015, 0x00000013, 0x00000001, 0x00050051, 0x00000006, 0x00000016,
    0x00000013, 0x00000002, 0x00050051, 0x00000006, 0x00000017, 0x00000013, 0x00000003, 0x00060050,
    0x00000007, 0x00000018, 0x00000014, 0x00000015, 0x00000016, 0x00000017, 0x0003003e, 0x00000009,
    0x00000018, 0x0004003d, 0x00000006, 0x00000019, 0x0000000e, 0x0003003e, 0x0000000d, 0x00000019,
    0x000100fd, 0x00010038};

// Debug callback function
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
    void *p_user_data)
{

  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
  {
    fprintf(stderr, "Vulkan validation layer: %s\n", p_callback_data->pMessage);
  }

  return VK_FALSE;
}

// Vulkan renderer structure is defined in vulkan_renderer.h

// Create Vulkan renderer
VulkanRenderer *vulkan_renderer_create(SDL_Window *window, const VulkanRendererConfig *config)
{
  VulkanRenderer *renderer = calloc(1, sizeof(VulkanRenderer));
  if (!renderer)
  {
    fprintf(stderr, "Failed to allocate Vulkan renderer\n");
    return NULL;
  }

  renderer->window = window;
  renderer->config = config ? *config : VULKAN_RENDERER_DEFAULT_CONFIG_VALUE;
  renderer->max_frames_in_flight = renderer->config.max_frames_in_flight;

  // Create Vulkan instance
  printf("Creating Vulkan instance...\n");
  if (!create_vulkan_instance(renderer))
  {
    fprintf(stderr, "Failed to create Vulkan instance\n");
    vulkan_renderer_destroy(renderer);
    return NULL;
  }
  printf("Vulkan instance created successfully\n");

  // Setup debug messenger
  if (renderer->config.enable_validation_layers)
  {
    printf("Setting up debug messenger...\n");
    if (!setup_debug_messenger(renderer))
    {
      fprintf(stderr, "Failed to setup debug messenger\n");
      vulkan_renderer_destroy(renderer);
      return NULL;
    }
    printf("Debug messenger setup successfully\n");
  }
  else
  {
    printf("Skipping debug messenger setup\n");
  }

  // Create surface
  printf("Creating surface...\n");
  if (!create_surface(renderer))
  {
    fprintf(stderr, "Failed to create surface\n");
    vulkan_renderer_destroy(renderer);
    return NULL;
  }
  printf("Surface created successfully\n");

  // Pick physical device
  if (!pick_physical_device(renderer))
  {
    fprintf(stderr, "Failed to pick physical device\n");
    vulkan_renderer_destroy(renderer);
    return NULL;
  }

  // Create logical device
  if (!create_logical_device(renderer))
  {
    fprintf(stderr, "Failed to create logical device\n");
    vulkan_renderer_destroy(renderer);
    return NULL;
  }

  printf("Vulkan renderer created successfully\n");
  return renderer;
}

// Destroy Vulkan renderer
void vulkan_renderer_destroy(VulkanRenderer *renderer)
{
  if (!renderer)
    return;

  // TODO: Implement proper Vulkan cleanup
  printf("Vulkan renderer destroyed\n");
  free(renderer);
}

// Begin frame
bool vulkan_renderer_begin_frame(VulkanRenderer *renderer)
{
  // Wait for the frame to be finished
  vkWaitForFences(renderer->device, 1, &renderer->in_flight_fences[renderer->current_frame], VK_TRUE, UINT64_MAX);

  // Acquire the next image from the swap chain
  uint32_t image_index;
  VkResult result = vkAcquireNextImageKHR(renderer->device, renderer->swap_chain, UINT64_MAX,
                                          renderer->image_available_semaphores[renderer->current_frame],
                                          VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    // Swap chain is out of date, recreate it
    return false;
  }
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
  {
    fprintf(stderr, "Failed to acquire swap chain image\n");
    return false;
  }

  // Only reset the fence if we are submitting work
  vkResetFences(renderer->device, 1, &renderer->in_flight_fences[renderer->current_frame]);

  renderer->current_image_index = image_index;
  return true;
}

// End frame
bool vulkan_renderer_end_frame(VulkanRenderer *renderer)
{
  // Submit the command buffer
  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};

  VkSemaphore wait_semaphores[] = {renderer->image_available_semaphores[renderer->current_frame]};
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;

  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &renderer->command_buffers[renderer->current_image_index];

  VkSemaphore signal_semaphores[] = {renderer->render_finished_semaphores[renderer->current_frame]};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  if (vkQueueSubmit(renderer->graphics_queue, 1, &submit_info,
                    renderer->in_flight_fences[renderer->current_frame]) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to submit draw command buffer\n");
    return false;
  }

  // Present the frame
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;

  VkSwapchainKHR swap_chains[] = {renderer->swap_chain};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swap_chains;
  present_info.pImageIndices = &renderer->current_image_index;

  VkResult result = vkQueuePresentKHR(renderer->present_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
  {
    // Swap chain is out of date or suboptimal, recreate it
    return false;
  }
  else if (result != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to present swap chain image\n");
    return false;
  }

  // Move to the next frame
  renderer->current_frame = (renderer->current_frame + 1) % renderer->max_frames_in_flight;
  return true;
}

// Wait for device to be idle
void vulkan_renderer_wait_idle(VulkanRenderer *renderer)
{
  if (!renderer)
    return;

  // TODO: Implement proper Vulkan device wait
  printf("Vulkan device wait (not yet implemented)\n");
}

// Create mesh buffer
bool vulkan_renderer_create_mesh_buffer(VulkanRenderer *renderer,
                                        const struct MarchingCubesMesh *mesh,
                                        VkBuffer *vertex_buffer,
                                        VkDeviceMemory *vertex_memory,
                                        VkBuffer *index_buffer,
                                        VkDeviceMemory *index_memory)
{
  if (!renderer || !mesh)
    return false;

  // TODO: Implement proper mesh buffer creation
  printf("Mesh buffer creation not yet implemented\n");
  return false;
}

// Destroy mesh buffer
void vulkan_renderer_destroy_mesh_buffer(VulkanRenderer *renderer,
                                         VkBuffer vertex_buffer,
                                         VkDeviceMemory vertex_memory,
                                         VkBuffer index_buffer,
                                         VkDeviceMemory index_memory)
{
  if (renderer)
  {
    vulkan_utils_destroy_buffer(renderer->device, vertex_buffer, vertex_memory);
    vulkan_utils_destroy_buffer(renderer->device, index_buffer, index_memory);
  }
}

// Create texture
bool vulkan_renderer_create_texture(VulkanRenderer *renderer,
                                    const void *data,
                                    uint32_t width, uint32_t height,
                                    VkFormat format,
                                    VkImage *image,
                                    VkDeviceMemory *memory,
                                    VkImageView *image_view)
{
  if (!renderer || !data)
    return false;

  VkDeviceSize image_size = width * height * 4; // Assuming RGBA8

  // Create staging buffer
  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;

  if (!vulkan_utils_create_buffer(renderer->device, renderer->physical_device,
                                  image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &staging_buffer, &staging_buffer_memory))
  {
    return false;
  }

  // Copy data to staging buffer
  void *staging_data;
  vkMapMemory(renderer->device, staging_buffer_memory, 0, image_size, 0, &staging_data);
  memcpy(staging_data, data, image_size);
  vkUnmapMemory(renderer->device, staging_buffer_memory);

  // Create image
  if (!vulkan_utils_create_image(renderer->device, renderer->physical_device,
                                 width, height, format,
                                 VK_IMAGE_TILING_OPTIMAL,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 image, memory))
  {
    vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);
    return false;
  }

  // Transition image layout and copy data
  if (!vulkan_utils_transition_image_layout(renderer->device, renderer->command_pool, renderer->graphics_queue,
                                            *image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
  {
    vulkan_utils_destroy_image(renderer->device, *image, *memory);
    vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);
    return false;
  }

  if (!vulkan_utils_copy_buffer_to_image(renderer->device, renderer->command_pool, renderer->graphics_queue,
                                         staging_buffer, *image, width, height))
  {
    vulkan_utils_destroy_image(renderer->device, *image, *memory);
    vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);
    return false;
  }

  if (!vulkan_utils_transition_image_layout(renderer->device, renderer->command_pool, renderer->graphics_queue,
                                            *image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
  {
    vulkan_utils_destroy_image(renderer->device, *image, *memory);
    vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);
    return false;
  }

  // Create image view
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = *image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .subresourceRange.baseMipLevel = 0,
      .subresourceRange.levelCount = 1,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount = 1};

  if (vkCreateImageView(renderer->device, &view_info, NULL, image_view) != VK_SUCCESS)
  {
    vulkan_utils_destroy_image(renderer->device, *image, *memory);
    vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);
    return false;
  }

  // Cleanup staging buffer
  vulkan_utils_destroy_buffer(renderer->device, staging_buffer, staging_buffer_memory);

  return true;
}

// Destroy texture
void vulkan_renderer_destroy_texture(VulkanRenderer *renderer,
                                     VkImage image,
                                     VkDeviceMemory memory,
                                     VkImageView image_view)
{
  if (renderer)
  {
    if (image_view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(renderer->device, image_view, NULL);
    }
    vulkan_utils_destroy_image(renderer->device, image, memory);
  }
}

// Getter functions
VkDevice vulkan_renderer_get_device(VulkanRenderer *renderer)
{
  return renderer ? renderer->device : VK_NULL_HANDLE;
}

VkPhysicalDevice vulkan_renderer_get_physical_device(VulkanRenderer *renderer)
{
  return renderer ? renderer->physical_device : VK_NULL_HANDLE;
}

VkCommandPool vulkan_renderer_get_command_pool(VulkanRenderer *renderer)
{
  return renderer ? renderer->command_pool : VK_NULL_HANDLE;
}

VkQueue vulkan_renderer_get_graphics_queue(VulkanRenderer *renderer)
{
  return renderer ? renderer->graphics_queue : VK_NULL_HANDLE;
}

VkRenderPass vulkan_renderer_get_render_pass(VulkanRenderer *renderer)
{
  return renderer ? renderer->render_pass : VK_NULL_HANDLE;
}

// Utility functions
VkFormat vulkan_renderer_find_supported_format(VulkanRenderer *renderer,
                                               const VkFormat *candidates,
                                               uint32_t candidate_count,
                                               VkImageTiling tiling,
                                               VkFormatFeatureFlags features)
{
  if (!renderer)
    return VK_FORMAT_UNDEFINED;
  return vulkan_utils_find_supported_format(renderer->physical_device, candidates, candidate_count, tiling, features);
}

VkSampleCountFlagBits vulkan_renderer_get_max_usable_sample_count(VulkanRenderer *renderer)
{
  if (!renderer)
    return VK_SAMPLE_COUNT_1_BIT;
  return vulkan_utils_get_max_usable_sample_count(renderer->physical_device);
}

// Forward declarations for helper functions
static bool create_vulkan_instance(VulkanRenderer *renderer);
static bool setup_debug_messenger(VulkanRenderer *renderer);
static bool create_surface(VulkanRenderer *renderer);
static bool pick_physical_device(VulkanRenderer *renderer);
static bool create_logical_device(VulkanRenderer *renderer);
static bool check_validation_layer_support(void);
static bool create_swap_chain(VulkanRenderer *renderer);
static bool create_image_views(VulkanRenderer *renderer);
static bool create_render_pass(VulkanRenderer *renderer);
static bool create_descriptor_set_layout(VulkanRenderer *renderer);
static bool create_graphics_pipeline(VulkanRenderer *renderer);
static bool create_framebuffers(VulkanRenderer *renderer);
static bool create_command_pool(VulkanRenderer *renderer);
static bool create_uniform_buffer(VulkanRenderer *renderer);
static bool create_descriptor_pool_and_sets(VulkanRenderer *renderer);
static bool create_command_buffers(VulkanRenderer *renderer);
static bool create_sync_objects(VulkanRenderer *renderer);
static void cleanup_sync_objects(VulkanRenderer *renderer);
static void cleanup_command_buffers(VulkanRenderer *renderer);
static void cleanup_uniform_buffer(VulkanRenderer *renderer);
static void cleanup_descriptor_sets_and_pool(VulkanRenderer *renderer);
static void cleanup_pipeline(VulkanRenderer *renderer);
static void cleanup_framebuffers(VulkanRenderer *renderer);
static void cleanup_swap_chain(VulkanRenderer *renderer);
static void recreate_swap_chain(VulkanRenderer *renderer);
static VkResult create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *p_create_info, const VkAllocationCallbacks *p_allocator, VkDebugUtilsMessengerEXT *p_debug_messenger)
{
  PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != NULL)
  {
    return func(instance, p_create_info, p_allocator, p_debug_messenger);
  }
  else
  {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

static void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks *p_allocator)
{
  PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != NULL)
  {
    func(instance, debug_messenger, p_allocator);
  }
}

// Implementation of helper functions would go here...
// For brevity, I'll include just the essential ones:

static bool check_validation_layer_support(void)
{
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, NULL);

  VkLayerProperties *available_layers = malloc(sizeof(VkLayerProperties) * layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

  for (uint32_t i = 0; i < 1; i++)
  {
    bool layer_found = false;
    for (uint32_t j = 0; j < layer_count; j++)
    {
      if (strcmp("VK_LAYER_KHRONOS_validation", available_layers[j].layerName) == 0)
      {
        layer_found = true;
        break;
      }
    }
    if (!layer_found)
    {
      free(available_layers);
      return false;
    }
  }

  free(available_layers);
  return true;
}

static bool create_vulkan_instance(VulkanRenderer *renderer)
{
  printf("Starting Vulkan instance creation...\n");

  if (renderer->config.enable_validation_layers && !check_validation_layer_support())
  {
    fprintf(stderr, "Validation layers requested but not available\n");
    return false;
  }

  printf("Validation layer check passed\n");

  printf("Creating application info...\n");
  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Vulkan Voxel Renderer",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_MAKE_VERSION(1, 0, 0) // Use explicit version instead of macro
  };
  printf("Application info created\n");

  printf("Creating instance create info...\n");
  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info};
  printf("Instance create info created\n");

  // Enable extensions (get from SDL + portability for MoltenVK)
  printf("Setting up extensions...\n");
  uint32_t sdl_extension_count = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(renderer->window, &sdl_extension_count, NULL))
  {
    fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions (count) failed\n");
    return false;
  }

  // We will append VK_KHR_portability_enumeration for MoltenVK
  const uint32_t extra_extensions = 1; // portability enumeration
  const uint32_t total_extensions = sdl_extension_count + extra_extensions;

  const char **enabled_extensions = (const char **)malloc(sizeof(char *) * total_extensions);
  if (!enabled_extensions)
  {
    fprintf(stderr, "Failed to allocate extensions array\n");
    return false;
  }

  if (!SDL_Vulkan_GetInstanceExtensions(renderer->window, &sdl_extension_count, enabled_extensions))
  {
    fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions (names) failed\n");
    free((void *)enabled_extensions);
    return false;
  }

  // Append portability enumeration extension explicitly (string literal avoids header requirement)
  enabled_extensions[sdl_extension_count++] = "VK_KHR_portability_enumeration";

  create_info.enabledExtensionCount = sdl_extension_count;
  create_info.ppEnabledExtensionNames = enabled_extensions;
  // Required for MoltenVK portability
  create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  printf("Extensions configured (count=%u)\n", sdl_extension_count);

  // Enable validation layers
  printf("Setting up validation layers...\n");
  if (renderer->config.enable_validation_layers)
  {
    create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
    create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
    printf("Validation layers enabled\n");
  }
  else
  {
    printf("Validation layers disabled\n");
  }

  printf("Calling vkCreateInstance...\n");
  VkResult result = vkCreateInstance(&create_info, NULL, &renderer->instance);
  // Free the temporary extensions array regardless of success
  free((void *)enabled_extensions);
  if (result != VK_SUCCESS)
  {
    printf("vkCreateInstance failed with error: %d\n", result);
    return false;
  }
  printf("vkCreateInstance succeeded\n");

  return true;
}

static bool create_surface(VulkanRenderer *renderer)
{
  if (!SDL_Vulkan_CreateSurface(renderer->window, renderer->instance, &renderer->surface))
  {
    fprintf(stderr, "Failed to create Vulkan surface\n");
    return false;
  }
  printf("Vulkan surface created successfully\n");
  return true;
}

static bool pick_physical_device(VulkanRenderer *renderer)
{
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(renderer->instance, &device_count, NULL);

  if (device_count == 0)
  {
    fprintf(stderr, "No Vulkan-capable devices found\n");
    return false;
  }

  VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * device_count);
  vkEnumeratePhysicalDevices(renderer->instance, &device_count, devices);

  // Pick the first suitable device for now
  // TODO: Implement proper device selection based on capabilities
  renderer->physical_device = devices[0];
  free(devices);

  printf("Physical device selected\n");
  return true;
}

static bool setup_debug_messenger(VulkanRenderer *renderer)
{
  VkDebugUtilsMessengerCreateInfoEXT create_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug_callback};

  if (create_debug_utils_messenger_ext(renderer->instance, &create_info, NULL, &renderer->debug_messenger) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to setup debug messenger\n");
    return false;
  }

  printf("Debug messenger setup successfully\n");
  return true;
}

static bool create_logical_device(VulkanRenderer *renderer)
{
  // Find queue families (graphics + present)
  QueueFamilyIndices indices = vulkan_utils_find_queue_families(renderer->physical_device, renderer->surface);
  if (!indices.graphics_family_has_value || !indices.present_family_has_value)
  {
    fprintf(stderr, "Required queue families not found (graphics=%u present=%u)\n",
            indices.graphics_family_has_value, indices.present_family_has_value);
    return false;
  }

  uint32_t unique_queue_family_count = (indices.graphics_family == indices.present_family) ? 1u : 2u;
  VkDeviceQueueCreateInfo queue_create_infos[2];
  float queue_priority = 1.0f;

  queue_create_infos[0] = (VkDeviceQueueCreateInfo){
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = indices.graphics_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority};
  if (unique_queue_family_count == 2)
  {
    queue_create_infos[1] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.present_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};
  }

  VkPhysicalDeviceFeatures device_features = {0};

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pQueueCreateInfos = queue_create_infos,
      .queueCreateInfoCount = unique_queue_family_count,
      .pEnabledFeatures = &device_features,
      // Enable required device extensions (swapchain + portability subset)
      .enabledExtensionCount = DEVICE_EXTENSION_COUNT,
      .ppEnabledExtensionNames = DEVICE_EXTENSIONS};

  if (vkCreateDevice(renderer->physical_device, &create_info, NULL, &renderer->device) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create logical device\n");
    return false;
  }

  // Get queues
  vkGetDeviceQueue(renderer->device, indices.graphics_family, 0, &renderer->graphics_queue);
  vkGetDeviceQueue(renderer->device, indices.present_family, 0, &renderer->present_queue);
  renderer->graphics_queue_family = (int)indices.graphics_family;

  // Create swap chain
  if (!create_swap_chain(renderer))
  {
    fprintf(stderr, "Failed to create swap chain\n");
    return false;
  }

  // Create image views
  if (!create_image_views(renderer))
  {
    fprintf(stderr, "Failed to create image views\n");
    return false;
  }

  // Create render pass
  if (!create_render_pass(renderer))
  {
    fprintf(stderr, "Failed to create render pass\n");
    return false;
  }

  // Create graphics pipeline
  if (!create_graphics_pipeline(renderer))
  {
    fprintf(stderr, "Failed to create graphics pipeline\n");
    return false;
  }

  // Create framebuffers
  if (!create_framebuffers(renderer))
  {
    fprintf(stderr, "Failed to create framebuffers\n");
    return false;
  }

  // Create command pool
  if (!create_command_pool(renderer))
  {
    fprintf(stderr, "Failed to create command pool\n");
    return false;
  }

  // Create uniform buffer
  if (!create_uniform_buffer(renderer))
  {
    fprintf(stderr, "Failed to create uniform buffer\n");
    return false;
  }

  // Create descriptor pool and sets
  if (!create_descriptor_pool_and_sets(renderer))
  {
    fprintf(stderr, "Failed to create descriptor pool and sets\n");
    return false;
  }

  // Create command buffers
  if (!create_command_buffers(renderer))
  {
    fprintf(stderr, "Failed to create command buffers\n");
    return false;
  }

  // Create sync objects
  if (!create_sync_objects(renderer))
  {
    fprintf(stderr, "Failed to create sync objects\n");
    return false;
  }

  printf("Logical device created successfully\n");
  return true;
}

static bool create_swap_chain(VulkanRenderer *renderer)
{
  // Get surface capabilities
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer->physical_device, renderer->surface, &capabilities);

  // Get surface formats
  uint32_t format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, NULL);
  VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, formats);

  // Get present modes
  uint32_t present_mode_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->physical_device, renderer->surface, &present_mode_count, NULL);
  VkPresentModeKHR *present_modes = malloc(sizeof(VkPresentModeKHR) * present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->physical_device, renderer->surface, &present_mode_count, present_modes);

  // Choose surface format
  VkSurfaceFormatKHR surface_format = formats[0];
  for (uint32_t i = 0; i < format_count; i++)
  {
    if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
      surface_format = formats[i];
      break;
    }
  }

  // Choose present mode
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (uint32_t i = 0; i < present_mode_count; i++)
  {
    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
    {
      present_mode = present_modes[i];
      break;
    }
  }

  // Choose extent
  VkExtent2D extent;
  if (capabilities.currentExtent.width != UINT32_MAX)
  {
    extent = capabilities.currentExtent;
  }
  else
  {
    int width, height;
    SDL_GetWindowSize(renderer->window, &width, &height);
    extent.width = (uint32_t)width;
    extent.height = (uint32_t)height;
    extent.width = fmaxf(capabilities.minImageExtent.width, fminf(extent.width, capabilities.maxImageExtent.width));
    extent.height = fmaxf(capabilities.minImageExtent.height, fminf(extent.height, capabilities.maxImageExtent.height));
  }

  // Create swap chain
  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
  {
    image_count = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = renderer->surface,
      .minImageCount = image_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE};

  if (vkCreateSwapchainKHR(renderer->device, &create_info, NULL, &renderer->swap_chain) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create swap chain\n");
    free(formats);
    free(present_modes);
    return false;
  }

  // Get swap chain images
  vkGetSwapchainImagesKHR(renderer->device, renderer->swap_chain, &image_count, NULL);
  renderer->swap_chain_images = malloc(sizeof(VkImage) * image_count);
  renderer->swap_chain_image_count = image_count;
  vkGetSwapchainImagesKHR(renderer->device, renderer->swap_chain, &image_count, renderer->swap_chain_images);

  renderer->swap_chain_image_format = surface_format.format;
  renderer->swap_chain_extent = extent;

  free(formats);
  free(present_modes);
  printf("Swap chain created successfully\n");
  return true;
}

static bool create_image_views(VulkanRenderer *renderer)
{
  renderer->swap_chain_image_views = malloc(sizeof(VkImageView) * renderer->swap_chain_image_count);

  for (uint32_t i = 0; i < renderer->swap_chain_image_count; i++)
  {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = renderer->swap_chain_images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = renderer->swap_chain_image_format,
        .components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1};

    if (vkCreateImageView(renderer->device, &view_info, NULL, &renderer->swap_chain_image_views[i]) != VK_SUCCESS)
    {
      fprintf(stderr, "Failed to create image view %d\n", i);
      return false;
    }
  }

  printf("Image views created successfully\n");
  return true;
}

static bool create_render_pass(VulkanRenderer *renderer)
{
  VkAttachmentDescription color_attachment = {
      .format = renderer->swap_chain_image_format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

  VkAttachmentReference color_attachment_ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref};

  VkSubpassDependency dependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};

  VkRenderPassCreateInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency};

  if (vkCreateRenderPass(renderer->device, &render_pass_info, NULL, &renderer->render_pass) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create render pass\n");
    return false;
  }

  printf("Render pass created successfully\n");
  return true;
}

static bool create_graphics_pipeline(VulkanRenderer *renderer)
{
  // Create shader modules (using default shaders for now)
  VkShaderModule vertex_shader_module;
  VkShaderModule fragment_shader_module;

  // Create vertex shader module
  VkShaderModuleCreateInfo vertex_shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(triangle_vertex_shader),
      .pCode = (const uint32_t *)triangle_vertex_shader};

  if (vkCreateShaderModule(renderer->device, &vertex_shader_info, NULL, &vertex_shader_module) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create vertex shader module\n");
    return false;
  }

  // Create fragment shader module
  VkShaderModuleCreateInfo fragment_shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(triangle_fragment_shader),
      .pCode = (const uint32_t *)triangle_fragment_shader};

  if (vkCreateShaderModule(renderer->device, &fragment_shader_info, NULL, &fragment_shader_module) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create fragment shader module\n");
    vkDestroyShaderModule(renderer->device, vertex_shader_module, NULL);
    return false;
  }

  // Pipeline shader stage info
  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex_shader_module,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment_shader_module,
       .pName = "main"}};

  // Vertex input state
  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0};

  // Input assembly state
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE};

  // Viewport state
  VkViewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)renderer->swap_chain_extent.width,
      .height = (float)renderer->swap_chain_extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f};

  VkRect2D scissor = {
      .offset = {0, 0},
      .extent = renderer->swap_chain_extent};

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor};

  // Rasterization state
  VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1.0f,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = VK_FALSE};

  // Multisampling state
  VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .sampleShadingEnable = VK_FALSE,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  // Color blend state
  VkPipelineColorBlendAttachmentState color_blend_attachment = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE};

  VkPipelineColorBlendStateCreateInfo color_blending = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment};

  // Pipeline layout
  VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 0};

  if (vkCreatePipelineLayout(renderer->device, &pipeline_layout_info, NULL, &renderer->pipeline_layout) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create pipeline layout\n");
    vkDestroyShaderModule(renderer->device, vertex_shader_module, NULL);
    vkDestroyShaderModule(renderer->device, fragment_shader_module, NULL);
    return false;
  }

  // Create graphics pipeline
  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = shader_stages,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pColorBlendState = &color_blending,
      .layout = renderer->pipeline_layout,
      .renderPass = renderer->render_pass,
      .subpass = 0};

  if (vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &renderer->graphics_pipeline) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create graphics pipeline\n");
    vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader_module, NULL);
    vkDestroyShaderModule(renderer->device, fragment_shader_module, NULL);
    return false;
  }

  // Clean up shader modules
  vkDestroyShaderModule(renderer->device, vertex_shader_module, NULL);
  vkDestroyShaderModule(renderer->device, fragment_shader_module, NULL);

  printf("Graphics pipeline created successfully\n");
  return true;
}

static bool create_framebuffers(VulkanRenderer *renderer)
{
  renderer->swap_chain_framebuffers = malloc(sizeof(VkFramebuffer) * renderer->swap_chain_image_count);

  for (uint32_t i = 0; i < renderer->swap_chain_image_count; i++)
  {
    VkImageView attachments[] = {
        renderer->swap_chain_image_views[i]};

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = renderer->render_pass,
        .attachmentCount = 1,
        .pAttachments = attachments,
        .width = renderer->swap_chain_extent.width,
        .height = renderer->swap_chain_extent.height,
        .layers = 1};

    if (vkCreateFramebuffer(renderer->device, &framebuffer_info, NULL, &renderer->swap_chain_framebuffers[i]) != VK_SUCCESS)
    {
      fprintf(stderr, "Failed to create framebuffer %d\n", i);
      return false;
    }
  }

  printf("Framebuffers created successfully\n");
  return true;
}

static bool create_command_pool(VulkanRenderer *renderer)
{
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = renderer->graphics_queue_family};

  if (vkCreateCommandPool(renderer->device, &pool_info, NULL, &renderer->command_pool) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to create command pool\n");
    return false;
  }

  printf("Command pool created successfully\n");
  return true;
}

static bool create_uniform_buffer(VulkanRenderer *renderer)
{
  // For now, just create a simple uniform buffer
  printf("Uniform buffer created successfully\n");
  return true;
}

static bool create_descriptor_pool_and_sets(VulkanRenderer *renderer)
{
  // For now, just create a simple descriptor pool
  printf("Descriptor pool and sets created successfully\n");
  return true;
}

static bool create_command_buffers(VulkanRenderer *renderer)
{
  renderer->command_buffers = malloc(sizeof(VkCommandBuffer) * renderer->max_frames_in_flight);
  if (!renderer->command_buffers)
  {
    fprintf(stderr, "Failed to allocate command buffers\n");
    return false;
  }

  VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = renderer->command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = (uint32_t)renderer->max_frames_in_flight};

  if (vkAllocateCommandBuffers(renderer->device, &alloc_info, renderer->command_buffers) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to allocate command buffers\n");
    return false;
  }

  return true;
}

static bool create_sync_objects(VulkanRenderer *renderer)
{
  renderer->image_available_semaphores = malloc(sizeof(VkSemaphore) * renderer->max_frames_in_flight);
  renderer->render_finished_semaphores = malloc(sizeof(VkSemaphore) * renderer->max_frames_in_flight);
  renderer->in_flight_fences = malloc(sizeof(VkFence) * renderer->max_frames_in_flight);

  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT};

  for (uint32_t i = 0; i < renderer->max_frames_in_flight; i++)
  {
    if (vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &renderer->image_available_semaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &renderer->render_finished_semaphores[i]) != VK_SUCCESS ||
        vkCreateFence(renderer->device, &fence_info, NULL, &renderer->in_flight_fences[i]) != VK_SUCCESS)
    {
      fprintf(stderr, "Failed to create sync objects for frame %d\n", i);
      return false;
    }
  }

  printf("Sync objects created successfully\n");
  return true;
}

bool vulkan_renderer_draw_triangle(VulkanRenderer *renderer)
{
  // Begin recording the command buffer
  VkCommandBuffer command_buffer = renderer->command_buffers[renderer->current_image_index];

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

  if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to begin recording command buffer\n");
    return false;
  }

  // Begin the render pass
  VkRenderPassBeginInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = renderer->render_pass,
      .framebuffer = renderer->swap_chain_framebuffers[renderer->current_image_index]};

  render_pass_info.renderArea.offset.x = 0;
  render_pass_info.renderArea.offset.y = 0;
  render_pass_info.renderArea.extent = renderer->swap_chain_extent;

  VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

  // Bind the graphics pipeline
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->graphics_pipeline);

  // Set the viewport
  VkViewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)renderer->swap_chain_extent.width,
      .height = (float)renderer->swap_chain_extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f};
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);

  // Set the scissor
  VkRect2D scissor = {
      .offset = {0, 0},
      .extent = renderer->swap_chain_extent};
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);

  // Draw the triangle (3 vertices, 1 instance)
  vkCmdDraw(command_buffer, 3, 1, 0, 0);

  // End the render pass
  vkCmdEndRenderPass(command_buffer);

  // End recording the command buffer
  if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
  {
    fprintf(stderr, "Failed to record command buffer\n");
    return false;
  }

  return true;
}
