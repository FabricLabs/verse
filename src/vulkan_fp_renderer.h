#ifndef VULKAN_FP_RENDERER_H
#define VULKAN_FP_RENDERER_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "vulkan_renderer.h"
#include "simple_world.h"

// Forward declarations
struct World;

// FPCamera structure (copied from fp_renderer.h to avoid conflicts)
typedef struct {
  float x, y, z;     // camera position in world space
  float yaw, pitch;  // radians; yaw around +Z (0 = +X), pitch up positive
  float fov_deg;     // vertical field of view in degrees
} FPCamera;

// First-person Vulkan renderer instance
typedef struct VulkanFPRenderer VulkanFPRenderer;

// Rendering modes for the Vulkan first-person renderer
typedef enum {
    VULKAN_FP_MODE_MESH = 0,      // Render using marching cubes mesh
    VULKAN_FP_MODE_RAY_MARCH = 1, // Render using compute shader ray marching
    VULKAN_FP_MODE_HYBRID = 2     // Hybrid approach (mesh for close, ray march for far)
} VulkanFPMode;

// Configuration for the Vulkan first-person renderer
typedef struct {
    VulkanFPMode mode;
    uint32_t render_distance;
    bool enable_frustum_culling;
    bool enable_occlusion_culling;
    bool enable_shadows;
    bool enable_post_processing;
    float fog_density;
    float fog_start_distance;
    float fog_end_distance;
} VulkanFPRendererConfig;

// Default configuration
static const VulkanFPRendererConfig VULKAN_FP_RENDERER_DEFAULT_CONFIG_VALUE = {
    .mode = VULKAN_FP_MODE_MESH,
    .render_distance = 32,
    .enable_frustum_culling = true,
    .enable_occlusion_culling = true,
    .enable_shadows = true,
    .enable_post_processing = true,
    .fog_density = 0.01f,
    .fog_start_distance = 10.0f,
    .fog_end_distance = 100.0f
};

// Function prototypes
VulkanFPRenderer* vulkan_fp_renderer_create(SDL_Window *window,
                                            const VulkanRendererConfig *vulkan_config,
                                            const VulkanFPRendererConfig *fp_config);
void vulkan_fp_renderer_destroy(VulkanFPRenderer *renderer);

// Basic rendering functions
bool vulkan_fp_renderer_begin_frame(VulkanFPRenderer *renderer);
bool vulkan_fp_renderer_end_frame(VulkanFPRenderer *renderer);

// Main rendering function (replaces fp_renderer_render)
void vulkan_fp_renderer_render(VulkanFPRenderer *renderer,
                               const struct World *world,
                               const FPCamera *camera,
                               int panel_x, int panel_y, int panel_w, int panel_h);

// Render with basic voxel rendering
bool vulkan_fp_renderer_render_basic(VulkanFPRenderer *renderer, const FPCamera *camera);

// Render with ray marching (compute shader)
void vulkan_fp_renderer_render_ray_march(VulkanFPRenderer *renderer,
                                         const struct World *world,
                                         const FPCamera *camera,
                                         int panel_x, int panel_y, int panel_w, int panel_h);

// Configuration and control
void vulkan_fp_renderer_set_mode(VulkanFPRenderer *renderer, VulkanFPMode mode);
void vulkan_fp_renderer_set_render_distance(VulkanFPRenderer *renderer, uint32_t distance);
void vulkan_fp_renderer_toggle_feature(VulkanFPRenderer *renderer, const char *feature_name, bool enable);

// World management
void vulkan_fp_renderer_update_world(VulkanFPRenderer *renderer,
                                    const struct World *world);
void vulkan_fp_renderer_invalidate_cache(VulkanFPRenderer *renderer);

// Camera and view management
void vulkan_fp_renderer_update_camera(VulkanFPRenderer *renderer,
                                     const FPCamera *camera);
void vulkan_fp_renderer_set_fov(VulkanFPRenderer *renderer, float fov_degrees);

// Performance and debugging
void vulkan_fp_renderer_get_stats(VulkanFPRenderer *renderer,
                                 uint32_t *draw_calls,
                                 uint32_t *triangles_rendered,
                                 float *frame_time_ms);
void vulkan_fp_renderer_enable_debug_rendering(VulkanFPRenderer *renderer, bool enable);

// Integration with existing fp_renderer
bool vulkan_fp_renderer_is_available(void);
void vulkan_fp_renderer_set_as_default(void);

#endif // VULKAN_FP_RENDERER_H
