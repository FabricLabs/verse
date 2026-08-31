#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct SimpleWorld;
struct MarchingCubesMesh;
struct VoxelMesh;

// Vulkan renderer configuration
typedef struct {
    uint32_t max_frames_in_flight;
    bool enable_validation_layers;
    bool enable_msaa;
    VkSampleCountFlagBits msaa_samples;
    uint32_t max_anisotropy;
    bool enable_sampler_anisotropy;
} VulkanRendererConfig;

// Default configuration
static const VulkanRendererConfig VULKAN_RENDERER_DEFAULT_CONFIG_VALUE = {
    .max_frames_in_flight = 2,
    .enable_validation_layers = false,  // Disable validation layers by default
    .enable_msaa = true,
    .msaa_samples = VK_SAMPLE_COUNT_4_BIT,
    .max_anisotropy = 16,
    .enable_sampler_anisotropy = true
};

// Vertex structure for Vulkan rendering
typedef struct {
    float position[3];    // x, y, z
    float normal[3];      // nx, ny, nz
    float color[4];       // r, g, b, a
    float tex_coord[2];   // u, v
} VulkanVertex;

// Uniform buffer object for camera matrices
typedef struct {
    float view_matrix[16];
    float projection_matrix[16];
    float model_matrix[16];
    float camera_pos[4];
    float light_pos[4];
    float time;
} VulkanUniformBuffer;

// Vulkan renderer instance
typedef struct VulkanRenderer {
    SDL_Window *window;
    VulkanRendererConfig config;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    int graphics_queue_family;

    VkSurfaceKHR surface;
    VkSwapchainKHR swap_chain;
    VkImage *swap_chain_images;
    uint32_t swap_chain_image_count;
    VkFormat swap_chain_image_format;
    VkExtent2D swap_chain_extent;
    VkImageView *swap_chain_image_views;

    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    VkFramebuffer *swap_chain_framebuffers;

    VkCommandPool command_pool;
    VkCommandBuffer *command_buffers;

    VkSemaphore *image_available_semaphores;
    VkSemaphore *render_finished_semaphores;
    VkFence *in_flight_fences;
    size_t max_frames_in_flight;
    size_t current_frame;
    uint32_t current_image_index;
} VulkanRenderer;

// Function prototypes
VulkanRenderer* vulkan_renderer_create(SDL_Window *window, const VulkanRendererConfig *config);
void vulkan_renderer_destroy(VulkanRenderer *renderer);

// Basic rendering functions
bool vulkan_renderer_begin_frame(VulkanRenderer *renderer);
bool vulkan_renderer_end_frame(VulkanRenderer *renderer);
bool vulkan_renderer_draw_triangle(VulkanRenderer *renderer);
void vulkan_renderer_wait_idle(VulkanRenderer *renderer);

// Mesh rendering
bool vulkan_renderer_create_mesh_buffer(VulkanRenderer *renderer,
                                       const struct MarchingCubesMesh *mesh,
                                       VkBuffer *vertex_buffer,
                                       VkDeviceMemory *vertex_memory,
                                       VkBuffer *index_buffer,
                                       VkDeviceMemory *index_memory);
bool vulkan_renderer_create_voxel_mesh_buffer(VulkanRenderer *renderer,
                                             const struct VoxelMesh *mesh,
                                             VkBuffer *vertex_buffer,
                                             VkDeviceMemory *vertex_memory,
                                             VkBuffer *index_buffer,
                                             VkDeviceMemory *index_memory);
void vulkan_renderer_destroy_mesh_buffer(VulkanRenderer *renderer,
                                        VkBuffer vertex_buffer,
                                        VkDeviceMemory vertex_memory,
                                        VkBuffer index_buffer,
                                        VkDeviceMemory index_memory);

// Texture management
bool vulkan_renderer_create_texture(VulkanRenderer *renderer,
                                   const void *data,
                                   uint32_t width, uint32_t height,
                                   VkFormat format,
                                   VkImage *image,
                                   VkDeviceMemory *memory,
                                   VkImageView *image_view);
void vulkan_renderer_destroy_texture(VulkanRenderer *renderer,
                                    VkImage image,
                                    VkDeviceMemory memory,
                                    VkImageView image_view);

// Utility functions
VkFormat vulkan_renderer_find_supported_format(VulkanRenderer *renderer,
                                              const VkFormat *candidates,
                                              uint32_t candidate_count,
                                              VkImageTiling tiling,
                                              VkFormatFeatureFlags features);
VkSampleCountFlagBits vulkan_renderer_get_max_usable_sample_count(VulkanRenderer *renderer);

// Getter functions for internal Vulkan objects
VkDevice vulkan_renderer_get_device(VulkanRenderer *renderer);
VkPhysicalDevice vulkan_renderer_get_physical_device(VulkanRenderer *renderer);
VkCommandPool vulkan_renderer_get_command_pool(VulkanRenderer *renderer);
VkQueue vulkan_renderer_get_graphics_queue(VulkanRenderer *renderer);
VkRenderPass vulkan_renderer_get_render_pass(VulkanRenderer *renderer);

#endif // VULKAN_RENDERER_H
