#include <inttypes.h>
#include "cpu_renderer_optimized.h"
#include "chunk_config.h"
#include <string.h>
#include <math.h>
#include <time.h>

// Global color table for fast lookup
SDL_Color g_cpu_color_table[VOXEL_COUNT];

// Global performance statistics
static CPURendererStats g_stats = {0};

// High-resolution timer for performance measurement
static uint64_t get_time_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Initialize the global color table (now using shared system)
void cpu_renderer_init_color_table(void)
{
  // Color table is now handled by shared isometric_get_voxel_color()
  // This function is kept for compatibility but no longer needed
}

// Default configuration with all optimizations enabled
CPURendererConfig cpu_renderer_config_default(void)
{
  CPURendererConfig config = {
      .flags = CPU_RENDER_OPT_ALL,
      .batch_size = 1024,
      .chunk_size_x = CHUNK_SIZE_X,
      .chunk_size_y = CHUNK_SIZE_Y,
      .chunk_size_z = CHUNK_SIZE_Z,
      .enable_grid = true,
      .enable_debug_overlay = false,
      .max_visible_voxels = 100000,
      .culling_margin = 32.0f};
  return config;
}

// Set optimization flags
void cpu_renderer_set_optimizations(CPURendererConfig *config, CPURenderOptimizations flags)
{
  if (config)
  {
    config->flags = flags;
  }
}

// Enable specific optimization
void cpu_renderer_enable_optimization(CPURendererConfig *config, CPURenderOptimizations opt)
{
  if (config)
  {
    config->flags |= opt;
  }
}

// Disable specific optimization
void cpu_renderer_disable_optimization(CPURendererConfig *config, CPURenderOptimizations opt)
{
  if (config)
  {
    config->flags &= ~opt;
  }
}

// Get current statistics
CPURendererStats cpu_renderer_get_stats(void)
{
  return g_stats;
}

// Reset statistics
void cpu_renderer_reset_stats(void)
{
  memset(&g_stats, 0, sizeof(g_stats));
}

// Print statistics to console
void cpu_renderer_print_stats(void)
{
  printf("=== CPU Renderer Performance Statistics ===\n");
  printf("Voxels processed: %" PRIu64 "\n", g_stats.voxels_processed);
  printf("Voxels culled: %" PRIu64 "\n", g_stats.voxels_culled);
  printf("Voxels rendered: %" PRIu64 "\n", g_stats.voxels_rendered);
  printf("Chunks processed: %" PRIu64 "\n", g_stats.chunks_processed);
  printf("Chunks culled: %" PRIu64 "\n", g_stats.chunks_culled);
  printf("Batches flushed: %" PRIu64 "\n", g_stats.batches_flushed);
  printf("Render time: %.3f ms\n", g_stats.render_time_ns / 1000000.0);
  printf("Cull time: %.3f ms\n", g_stats.cull_time_ns / 1000000.0);
  printf("Draw time: %.3f ms\n", g_stats.draw_time_ns / 1000000.0);
  printf("Culling efficiency: %.1f%%\n",
         g_stats.voxels_processed > 0 ? (100.0 * g_stats.voxels_culled) / g_stats.voxels_processed : 0.0);
  printf("==========================================\n");
}

// Helper functions for VoxelBatch processing
static void flush_voxel_batch(const VoxelBatch *batch, SDL_Renderer *sdl_renderer)
{
  if (!batch || batch->count == 0)
    return;

  // Group by color for minimal state changes
  for (int i = 0; i < batch->count; i++)
  {
    uint32_t packed_color = batch->colors[i];
    SDL_Color color = {
        (uint8_t)(packed_color >> 24),
        (uint8_t)(packed_color >> 16),
        (uint8_t)(packed_color >> 8),
        (uint8_t)packed_color};

    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(sdl_renderer, &batch->rects[i]);
  }
  g_stats.batches_flushed++;
}

static void add_to_voxel_batch(int x, int y, int w, int h, SDL_Color color,
                               VoxelBatch *batches, int *batch_count, int max_batches,
                               SDL_Renderer *sdl_renderer)
{
  if (!batches || *batch_count >= max_batches)
  {
    // Fallback to immediate rendering
    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(sdl_renderer, &rect);
    return;
  }

  VoxelBatch *current_batch = &batches[*batch_count];
  if (current_batch->count >= 64)
  {
    flush_voxel_batch(current_batch, sdl_renderer);
    (*batch_count)++;
    if (*batch_count >= max_batches)
    {
      *batch_count = 0; // Wrap around
    }
    current_batch = &batches[*batch_count];
    current_batch->count = 0;
  }

  int idx = current_batch->count++;
  current_batch->rects[idx] = (SDL_Rect){x, y, w, h};
  current_batch->colors[idx] = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
}

// Extremely optimized CPU renderer with all modern techniques
void cpu_renderer_render_optimized(IsometricRenderer *renderer,
                                   SDL_Renderer *sdl_renderer,
                                   const CPURendererConfig *config)
{
  if (!renderer || !sdl_renderer || !config)
    return;

  uint64_t start_time = get_time_ns();
  uint64_t cull_start = 0, draw_start = 0;

  // Initialize color table if not done
  static bool color_table_initialized = false;
  if (!color_table_initialized)
  {
    cpu_renderer_init_color_table();
    color_table_initialized = true;
  }

  World *w = renderer->game_worlds ? renderer->game_worlds->home_world : NULL;
  if (!w)
    return;

  // Clear background
  SDL_SetRenderDrawColor(sdl_renderer, 20, 22, 35, 255);
  SDL_RenderClear(sdl_renderer);

  // Early exit for invalid screen dimensions
  int screen_w = renderer->screen_width;
  int screen_h = renderer->screen_height;
  if (screen_w <= 0 || screen_h <= 0)
    return;

  // Calculate optimal cell dimensions
  float cell_wf = (float)screen_w / (float)w->width;
  float cell_hf = (float)screen_h / (float)w->height;
  int cell_w = (int)floorf(cell_wf);
  int cell_h = (int)floorf(cell_hf);
  if (cell_w < 2)
    cell_w = 2;
  if (cell_h < 2)
    cell_h = 2;

  // Use a simple, standard scale factor for isometric rendering
  // This gives a good view without over-engineering the scaling
  float scale_factor = 1.0f;

  // Apply the standard scale
  cell_w = (int)(cell_w * scale_factor);
  cell_h = (int)(cell_h * scale_factor);
  if (cell_w < 3) cell_w = 3;  // Minimum 3x3 pixels for good visibility
  if (cell_h < 3) cell_h = 3;

  // For isometric rendering, we need to process multiple Z slices
  // Start from the top and work down to create the isometric view
  int start_z = (int)w->depth - 1;
  int end_z = 0;

  // Calculate world center for camera positioning (no debug output for performance)
  int world_center_x = (int)w->width / 2;
  int world_center_y = (int)w->height / 2;
  int world_center_z = (int)w->depth / 2;

  // Start culling phase timing
  cull_start = get_time_ns();

  // Chunk-based rendering with aggressive culling
  const int chunk_size_x = config->chunk_size_x;
  const int chunk_size_y = config->chunk_size_y;
  const int chunk_size_z = config->chunk_size_z;

  const int cx_count = (int)((w->width + chunk_size_x - 1) / chunk_size_x);
  const int cy_count = (int)((w->height + chunk_size_y - 1) / chunk_size_y);
  const int cz_count = (int)((w->depth + chunk_size_z - 1) / chunk_size_z);

  // Calculate screen center that will properly center the isometric world
  int screen_center_x = screen_w / 2;
  int screen_center_y = screen_h / 2;

  // For isometric rendering, center the camera on the world center (64,64,64)
  // This ensures the camera is exactly where the island is geometrically centered
  // (world_center_x, world_center_y, world_center_z are already defined above)

  // Calculate where the world center projects to in screen space
  // Use the same fixed isometric angles for consistency with voxel rendering
  const float iso_x_mult = 0.866f;  // cos(30°) for consistent X-Y projection
  const float iso_y_mult = 0.5f;    // sin(30°) for consistent X-Y projection
  const float iso_z_mult = 0.7f;    // Z depth multiplier for 3D separation

  int center_iso_x = (int)(((world_center_x - world_center_y) * iso_x_mult * cell_w) + 0.5f);
  int center_iso_y = (int)(((world_center_x + world_center_y) * iso_y_mult * cell_h) - (world_center_z * iso_z_mult * cell_h) + 0.5f);

  // Adjust screen center so the world center appears at screen center
  screen_center_x -= center_iso_x;
  screen_center_y -= center_iso_y;

  // Screen center calculated for optimal camera positioning

  // World bounds with culling margin
  int world_min_x = 0, world_max_x = (int)w->width - 1;
  int world_min_y = 0, world_max_y = (int)w->height - 1;
  int world_min_z = 0, world_max_z = (int)w->depth - 1;

  int extra_cells = (int)(config->culling_margin / fminf(cell_w, cell_h)) + 2;
  world_min_x = (int)fmaxf(0, world_min_x - extra_cells);
  world_max_x = (int)fminf((int)w->width - 1, world_max_x + extra_cells);
  world_min_y = (int)fmaxf(0, world_min_y - extra_cells);
  world_max_y = (int)fminf((int)w->height - 1, world_max_y + extra_cells);
  world_min_z = (int)fmaxf(0, world_min_z - extra_cells);
  world_max_z = (int)fminf((int)w->depth - 1, world_max_z + extra_cells);

  // Occupancy bitfield optimization
  bool use_occupancy_bits = false;
  if (config->flags & CPU_RENDER_OPT_OCCUPANCY_BITS)
  {
    // Note: occupancy_bits is not fully implemented in current world structure
    // This is a placeholder for future optimization
    use_occupancy_bits = false;
  }

  // Batch rendering with memory-aligned structures
  VoxelBatch *batches = NULL;
  int batch_count = 0;
  int max_batches = (config->batch_size + 63) / 64;

  if (config->flags & CPU_RENDER_OPT_BATCH_RENDERING)
  {
    batches = aligned_alloc(32, max_batches * sizeof(VoxelBatch));
    if (batches)
    {
      memset(batches, 0, max_batches * sizeof(VoxelBatch));
    }
  }

  // Simple direct rendering in Z order (back to front)
  // This naturally gives correct occlusion for isometric view

  // Process voxels directly in depth order (back to front)
  // Render from low Z to high Z so closer voxels cover distant ones
  for (int z = end_z; z <= start_z; z++)
  {
    for (int y = world_min_y; y <= world_max_y; y++)
    {
      for (int x = world_min_x; x <= world_max_x; x++)
      {
        if (!cpu_renderer_bounds_check(x, y, z, w->width, w->height, w->depth))
        {
          continue;
        }

        g_stats.voxels_processed++;

        // Fast voxel access
        Voxel *voxel = world_pos_in_bounds_fast(w, x, y, z) ? world_voxel_ptr_fast(w, x, y, z) : NULL;

        if (!voxel || voxel->type == VOXEL_AIR)
        {
          g_stats.voxels_culled++;
          continue;
        }

        // Use shared canonical color system
        SDL_Color color = isometric_get_voxel_color(voxel->type);

        // Apply face-based shading for better visual structure
        if (config->flags & CPU_RENDER_OPT_FACE_CULLING) {
            // Determine which faces are visible and apply shading
            bool top_face_visible = true;
            bool side_faces_visible = false;

            // Check if top face is covered (voxel above exists and is solid)
            if (z + 1 < (int)w->depth) {
                Voxel *voxel_above = world_pos_in_bounds_fast(w, x, y, z + 1) ?
                                    world_voxel_ptr_fast(w, x, y, z + 1) : NULL;
                if (voxel_above && voxel_above->type != VOXEL_AIR) {
                    top_face_visible = false;
                }
            }

            // Check if side faces are visible (voxels to sides exist and are solid)
            if (x + 1 < (int)w->width) {
                Voxel *voxel_right = world_pos_in_bounds_fast(w, x + 1, y, z) ?
                                    world_voxel_ptr_fast(w, x + 1, y, z) : NULL;
                if (voxel_right && voxel_right->type != VOXEL_AIR) {
                    side_faces_visible = true;
                }
            }
            if (x > 0) {
                Voxel *voxel_left = world_pos_in_bounds_fast(w, x - 1, y, z) ?
                                   world_voxel_ptr_fast(w, x - 1, y, z) : NULL;
                if (voxel_left && voxel_left->type != VOXEL_AIR) {
                    side_faces_visible = true;
                }
            }
            if (y + 1 < (int)w->height) {
                Voxel *voxel_front = world_pos_in_bounds_fast(w, x, y + 1, z) ?
                                    world_voxel_ptr_fast(w, x, y + 1, z) : NULL;
                if (voxel_front && voxel_front->type != VOXEL_AIR) {
                    side_faces_visible = true;
                }
            }
            if (y > 0) {
                Voxel *voxel_back = world_pos_in_bounds_fast(w, x, y - 1, z) ?
                                   world_voxel_ptr_fast(w, x, y - 1, z) : NULL;
                if (voxel_back && voxel_back->type != VOXEL_AIR) {
                    side_faces_visible = true;
                }
            }

            // Apply shading based on face visibility
            if (!top_face_visible) {
                // Top face is covered - apply darker shading (like a side face)
                color.r = (uint8_t)(color.r * 0.7f);
                color.g = (uint8_t)(color.g * 0.7f);
                color.b = (uint8_t)(color.b * 0.7f);
            } else if (side_faces_visible) {
                // Top face is visible but has adjacent voxels - apply subtle shading
                color.r = (uint8_t)(color.r * 0.9f);
                color.g = (uint8_t)(color.g * 0.9f);
                color.b = (uint8_t)(color.b * 0.9f);
            }
            // If top face is visible and no adjacent voxels, keep original color
        }

        // Calculate screen position (standard isometric projection)
        // Use fixed isometric angles for consistent view regardless of world size
        // Standard isometric: 30° angles, so multipliers are cos(30°) = 0.866 and sin(30°) = 0.5
        const float iso_x_mult = 0.866f;  // cos(30°) for consistent X-Y projection
        const float iso_y_mult = 0.5f;    // sin(30°) for consistent X-Y projection
        const float iso_z_mult = 0.7f;    // Z depth multiplier for 3D separation

        int screen_x = screen_center_x + (int)(((x - y) * iso_x_mult * cell_w) + 0.5f);
        int screen_y = screen_center_y + (int)(((x + y) * iso_y_mult * cell_h) - (z * iso_z_mult * cell_h) + 0.5f);

        // Basic screen bounds checking
        if (config->flags & CPU_RENDER_OPT_SCREEN_CULLING)
        {
          if (screen_x < -cell_w || screen_x > screen_w ||
              screen_y < -cell_h || screen_y > screen_h)
          {
            g_stats.voxels_culled++;
            continue;
          }
        }

        // Render the voxel immediately
        SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
        SDL_Rect rect = {screen_x, screen_y, cell_w, cell_h};
        SDL_RenderFillRect(sdl_renderer, &rect);
        g_stats.voxels_rendered++;
      }
    }
  }

  // End culling phase timing
  g_stats.cull_time_ns = get_time_ns() - cull_start;
  draw_start = get_time_ns();

  // Flush remaining batches
  if (batches)
  {
    for (int i = 0; i <= batch_count; i++)
    {
      flush_voxel_batch(&batches[i], sdl_renderer);
    }
    free(batches);
  }

  // Render grid lines if enabled
  if (config->enable_grid)
  {
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 40);

    // Batch grid lines using standard isometric projection
    // Use the same fixed angles for consistency with voxel rendering
    const float iso_x_mult = 0.866f;  // cos(30°) for consistent X-Y projection
    const float iso_y_mult = 0.5f;    // sin(30°) for consistent X-Y projection

    // X-axis grid lines (vertical lines in isometric view)
    for (uint32_t gx = 0; gx <= w->width; gx++)
    {
      int sx = screen_center_x + (int)(gx * iso_x_mult * cell_w);
      SDL_RenderDrawLine(sdl_renderer, sx, 0, sx, screen_h);
    }
    // Y-axis grid lines (horizontal lines in isometric view)
    for (uint32_t gy = 0; gy <= w->height; gy++)
    {
      int sy = screen_center_y + (int)(gy * iso_y_mult * cell_h);
      SDL_RenderDrawLine(sdl_renderer, 0, sy, screen_w, sy);
    }
  }

  // End drawing phase timing
  g_stats.draw_time_ns = get_time_ns() - draw_start;
  g_stats.render_time_ns = get_time_ns() - start_time;
}
