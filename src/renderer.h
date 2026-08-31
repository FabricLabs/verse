#ifndef RENDERER_H
#define RENDERER_H

#include "world.h"
#include "engine.h"

// Renderer Function Declarations
void renderer_init();
void renderer_clear_screen();
void renderer_draw_world(World* world, int player_x, int player_y, int player_z);
void renderer_draw_actor_info(Actor* actor);
void renderer_draw_inventory(Actor* actor);
void renderer_draw_character_sheet(Actor* actor);
void renderer_draw_battle_interface(Actor* player, Actor* enemy);
void renderer_draw_navigation_map(World* current_world, World** connected_worlds, int num_connected);
void renderer_draw_building_interface(World* world, int x, int y, int z);
void renderer_set_debug_mode(int enabled);
void renderer_set_view_distance(int distance);
void renderer_show_coordinates(int show);

// Renderer Constants
#define DEFAULT_VIEW_DISTANCE 10
#define MAX_VIEW_DISTANCE 50
#define TERMINAL_WIDTH 80
#define TERMINAL_HEIGHT 24

#endif // RENDERER_H
