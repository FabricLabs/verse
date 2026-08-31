#include <SDL2/SDL.h>
#include "fp_renderer.h"

void fp_renderer_gl_init(void) {}
void fp_renderer_invalidate_cache(void) {}
void fp_renderer_force_cache_rebuild(const World *world) {(void)world;}
void fp_renderer_reset_warmup(void) {}
void fp_renderer_set_cached_mesh(const World *world, const VoxelMesh *mesh) {(void)world;(void)mesh;}
void fp_renderer_set_world_octree(const World *world, Octree *octree) {(void)world;(void)octree;}
Octree *fp_renderer_get_world_octree(const World *world) {(void)world; return NULL;}
void fp_renderer_build_world_octree(const World *world) {(void)world;}
void fp_renderer_set_mode(FPMode mode) {(void)mode;}
void fp_renderer_enable_gpu_mesh(bool enable) {(void)enable;}
void fp_renderer_render(SDL_Renderer *ren, const World *world, const FPCamera *cam, int x, int y, int w, int h) {
	(void)ren;(void)world;(void)cam;(void)x;(void)y;(void)w;(void)h;
}
void fp_renderer_render_neighbors(SDL_Renderer *ren, const GameWorlds *game_worlds, const FPCamera *cam, int panel_x, int panel_y, int panel_w, int panel_h, int layer) {
	(void)ren;(void)game_worlds;(void)cam;(void)panel_x;(void)panel_y;(void)panel_w;(void)panel_h;(void)layer;
}
