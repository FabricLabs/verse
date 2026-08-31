#include "vulkan_fp_renderer.h"
#include "vulkan_renderer.h"
#include "vulkan_utils.h"
#include "vulkan_shaders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// First-person Vulkan renderer structure
struct VulkanFPRenderer {
    // Core Vulkan renderer
    VulkanRenderer *vulkan_renderer;

    // Configuration
    VulkanFPRendererConfig config;

    // Mesh data
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    uint32_t index_count;

    // Camera and view data
    VulkanUniformBuffer uniform_buffer_data;
    VkBuffer uniform_buffer;
    VkDeviceMemory uniform_buffer_memory;
    void *uniform_buffer_mapped;

    // Pipeline for voxel rendering
    VkPipelineLayout voxel_pipeline_layout;
    VkPipeline voxel_pipeline;

    // Descriptor sets
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;

    // Textures
    VkImage voxel_texture;
    VkDeviceMemory voxel_texture_memory;
    VkImageView voxel_texture_view;
    VkSampler voxel_sampler;

    // Performance tracking
    uint32_t draw_calls;
    uint32_t triangles_rendered;
    float frame_time_ms;
    Uint64 last_frame_time;

    // Debug rendering
    bool debug_rendering_enabled;

    // Window reference
    SDL_Window *window;
};

// Global state for integration
static VulkanFPRenderer *g_vulkan_fp_renderer = NULL;
static bool g_vulkan_available = false;

// Create first-person Vulkan renderer
VulkanFPRenderer* vulkan_fp_renderer_create(SDL_Window *window,
                                            const VulkanRendererConfig *vulkan_config,
                                            const VulkanFPRendererConfig *fp_config) {
    VulkanFPRenderer *renderer = calloc(1, sizeof(VulkanFPRenderer));
    if (!renderer) {
        fprintf(stderr, "Failed to allocate Vulkan FP renderer\n");
        return NULL;
    }

    renderer->window = window;
    renderer->config = fp_config ? *fp_config : VULKAN_FP_RENDERER_DEFAULT_CONFIG_VALUE;

    // Create core Vulkan renderer
    renderer->vulkan_renderer = vulkan_renderer_create(window, vulkan_config);
    if (!renderer->vulkan_renderer) {
        fprintf(stderr, "Failed to create Vulkan renderer\n");
        vulkan_fp_renderer_destroy(renderer);
        return NULL;
    }

    // TODO: Implement proper Vulkan initialization
    printf("Vulkan first-person renderer structure created (Vulkan initialization not yet implemented)\n");
    return renderer;
}

// Destroy first-person Vulkan renderer
void vulkan_fp_renderer_destroy(VulkanFPRenderer *renderer) {
    if (!renderer) return;

    // Wait for device to be idle
    vulkan_renderer_wait_idle(renderer->vulkan_renderer);

    // Cleanup mesh buffers
    if (renderer->vertex_buffer != VK_NULL_HANDLE) {
        vulkan_renderer_destroy_mesh_buffer(renderer->vulkan_renderer,
                                          renderer->vertex_buffer, renderer->vertex_memory,
                                          renderer->index_buffer, renderer->index_memory);
    }

    // Cleanup uniform buffer
    if (renderer->uniform_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan_renderer_get_device(renderer->vulkan_renderer),
                       renderer->uniform_buffer, NULL);
    }
    if (renderer->uniform_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkan_renderer_get_device(renderer->vulkan_renderer),
                    renderer->uniform_buffer_memory, NULL);
    }

    // Cleanup descriptor sets and pool
    if (renderer->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan_renderer_get_device(renderer->vulkan_renderer),
                              renderer->descriptor_pool, NULL);
    }
    if (renderer->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan_renderer_get_device(renderer->vulkan_renderer),
                                   renderer->descriptor_set_layout, NULL);
    }

    // Cleanup pipeline
    if (renderer->voxel_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan_renderer_get_device(renderer->vulkan_renderer),
                         renderer->voxel_pipeline, NULL);
    }
    if (renderer->voxel_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan_renderer_get_device(renderer->vulkan_renderer),
                              renderer->voxel_pipeline_layout, NULL);
    }

    // Cleanup texture and sampler
    if (renderer->voxel_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkan_renderer_get_device(renderer->vulkan_renderer),
                        renderer->voxel_sampler, NULL);
    }
    vulkan_renderer_destroy_texture(renderer->vulkan_renderer,
                                   renderer->voxel_texture,
                                   renderer->voxel_texture_memory,
                                   renderer->voxel_texture_view);

    // Cleanup core Vulkan renderer
    if (renderer->vulkan_renderer) {
        vulkan_renderer_destroy(renderer->vulkan_renderer);
    }

    free(renderer);
}

// Begin frame
bool vulkan_fp_renderer_begin_frame(VulkanFPRenderer *renderer) {
    if (!renderer || !renderer->vulkan_renderer) return false;

    // Update performance tracking
    Uint64 current_time = SDL_GetTicks();
    renderer->frame_time_ms = (float)(current_time - renderer->last_frame_time);
    renderer->last_frame_time = current_time;

    // Reset counters
    renderer->draw_calls = 0;
    renderer->triangles_rendered = 0;

    return vulkan_renderer_begin_frame(renderer->vulkan_renderer);
}

// End frame
bool vulkan_fp_renderer_end_frame(VulkanFPRenderer *renderer) {
    if (!renderer || !renderer->vulkan_renderer) return false;

    return vulkan_renderer_end_frame(renderer->vulkan_renderer);
}

// Main rendering function
void vulkan_fp_renderer_render(VulkanFPRenderer *renderer,
                               const struct World *world,
                               const FPCamera *camera,
                               int panel_x, int panel_y, int panel_w, int panel_h) {
    if (!renderer || !world || !camera) return;

    // TODO: Implement proper Vulkan rendering
    printf("Vulkan rendering not yet implemented\n");
}

// Render with marching cubes mesh
void vulkan_fp_renderer_render_mesh(VulkanFPRenderer *renderer,
                                   const struct World *world,
                                   const struct VoxelMesh *mesh,
                                   const FPCamera *camera,
                                   int panel_x, int panel_y, int panel_w, int panel_h) {
    if (!renderer || !world || !mesh || !camera) return;

    // TODO: Implement mesh rendering
    printf("Mesh rendering not yet implemented\n");
}

// Render with ray marching (compute shader)
void vulkan_fp_renderer_render_ray_march(VulkanFPRenderer *renderer,
                                         const struct World *world,
                                         const FPCamera *camera,
                                         int panel_x, int panel_y, int panel_w, int panel_h) {
    if (!renderer || !world || !camera) return;

    // TODO: Implement ray marching
    printf("Ray marching not yet implemented\n");
}

// Render with basic voxel rendering
bool vulkan_fp_renderer_render_basic(VulkanFPRenderer *renderer, const FPCamera *camera) {
    if (!renderer || !renderer->vulkan_renderer) {
        return false;
    }

    // Begin the frame
    if (!vulkan_renderer_begin_frame(renderer->vulkan_renderer)) {
        return false;
    }

    // Draw the triangle
    if (!vulkan_renderer_draw_triangle(renderer->vulkan_renderer)) {
        return false;
    }

    // End the frame
    if (!vulkan_renderer_end_frame(renderer->vulkan_renderer)) {
        return false;
    }

    return true;
}

// Configuration and control
void vulkan_fp_renderer_set_mode(VulkanFPRenderer *renderer, VulkanFPMode mode) {
    if (renderer) {
        renderer->config.mode = mode;
    }
}

void vulkan_fp_renderer_set_render_distance(VulkanFPRenderer *renderer, uint32_t distance) {
    if (renderer) {
        renderer->config.render_distance = distance;
    }
}

void vulkan_fp_renderer_toggle_feature(VulkanFPRenderer *renderer, const char *feature_name, bool enable) {
    if (!renderer || !feature_name) return;

    if (strcmp(feature_name, "frustum_culling") == 0) {
        renderer->config.enable_frustum_culling = enable;
    } else if (strcmp(feature_name, "occlusion_culling") == 0) {
        renderer->config.enable_occlusion_culling = enable;
    } else if (strcmp(feature_name, "shadows") == 0) {
        renderer->config.enable_shadows = enable;
    } else if (strcmp(feature_name, "post_processing") == 0) {
        renderer->config.enable_post_processing = enable;
    }
}

// Mesh management
bool vulkan_fp_renderer_update_mesh(VulkanFPRenderer *renderer,
                                   const struct World *world,
                                   const struct VoxelMesh *mesh) {
    if (!renderer || !world || !mesh) return false;

    // TODO: Implement mesh updating
    printf("Mesh updating not yet implemented\n");
    return false;
}

void vulkan_fp_renderer_invalidate_mesh_cache(VulkanFPRenderer *renderer) {
    if (renderer) {
        // Force recreation of mesh buffers on next render
        if (renderer->vertex_buffer != VK_NULL_HANDLE) {
            vulkan_renderer_destroy_mesh_buffer(renderer->vulkan_renderer,
                                              renderer->vertex_buffer, renderer->vertex_memory,
                                              renderer->index_buffer, renderer->index_memory);
            renderer->vertex_buffer = VK_NULL_HANDLE;
            renderer->index_buffer = VK_NULL_HANDLE;
        }
    }
}

// Camera and view management
void vulkan_fp_renderer_update_camera(VulkanFPRenderer *renderer,
                                     const FPCamera *camera) {
    if (!renderer || !camera) return;

    // TODO: Implement camera updating
    printf("Camera updating not yet implemented\n");
}

void vulkan_fp_renderer_set_fov(VulkanFPRenderer *renderer, float fov_degrees) {
    if (!renderer) return;

    // TODO: Implement FOV updating
    printf("FOV updating not yet implemented\n");
}

// Performance and debugging
void vulkan_fp_renderer_get_stats(VulkanFPRenderer *renderer,
                                 uint32_t *draw_calls,
                                 uint32_t *triangles_rendered,
                                 float *frame_time_ms) {
    if (renderer) {
        if (draw_calls) *draw_calls = renderer->draw_calls;
        if (triangles_rendered) *triangles_rendered = renderer->triangles_rendered;
        if (frame_time_ms) *frame_time_ms = renderer->frame_time_ms;
    }
}

void vulkan_fp_renderer_enable_debug_rendering(VulkanFPRenderer *renderer, bool enable) {
    if (renderer) {
        renderer->debug_rendering_enabled = enable;
    }
}

// Integration with existing fp_renderer
bool vulkan_fp_renderer_is_available(void) {
    // Check if Vulkan is available by trying to create a basic instance
    VkInstance test_instance;

    // Try with different API versions
    uint32_t api_versions[] = {
        VK_API_VERSION_1_0,
        VK_MAKE_VERSION(1, 0, 0),
        VK_MAKE_VERSION(1, 1, 0),
        VK_MAKE_VERSION(1, 2, 0)
    };

    for (int i = 0; i < 4; i++) {
        VkApplicationInfo app_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "Vulkan Test",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = api_versions[i]
        };

    // First try without extensions
            VkInstanceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info
        };

        VkResult result = vkCreateInstance(&create_info, NULL, &test_instance);
        if (result == VK_SUCCESS) {
            vkDestroyInstance(test_instance, NULL);
            printf("Vulkan instance creation test successful with API version %d.%d.%d (no extensions)\n",
                   VK_VERSION_MAJOR(api_versions[i]), VK_VERSION_MINOR(api_versions[i]), VK_VERSION_PATCH(api_versions[i]));
            return true;
        }

        printf("Vulkan instance creation with API version %d.%d.%d failed with error: %d\n",
               VK_VERSION_MAJOR(api_versions[i]), VK_VERSION_MINOR(api_versions[i]), VK_VERSION_PATCH(api_versions[i]), result);
    }

    // If we get here, try with extensions
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Test",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };

    // Try with minimal extensions
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions
    };

    VkResult result = vkCreateInstance(&create_info, NULL, &test_instance);
    if (result == VK_SUCCESS) {
        vkDestroyInstance(test_instance, NULL);
        printf("Vulkan instance creation test successful (with surface extension)\n");
        return true;
    }

    printf("Vulkan instance creation with surface extension failed with error: %d\n", result);

    // Try with MoltenVK extension
#ifdef VK_USE_PLATFORM_MACOS_MVK
    const char* molten_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_MVK_MACOS_SURFACE_EXTENSION_NAME
    };

    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = molten_extensions;

    result = vkCreateInstance(&create_info, NULL, &test_instance);
    if (result == VK_SUCCESS) {
        vkDestroyInstance(test_instance, NULL);
        printf("Vulkan instance creation test successful (with MoltenVK extensions)\n");
        return true;
    }

    printf("Vulkan instance creation with MoltenVK extensions failed with error: %d\n", result);
#endif

    return false;
}

void vulkan_fp_renderer_set_as_default(void) {
    if (g_vulkan_fp_renderer) {
        // Set this renderer as the default for the existing fp_renderer
        // This would require modifying the existing fp_renderer to use Vulkan
        printf("Vulkan FP renderer set as default\n");
    }
}


