#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

#define WORLD_SIZE 32
#define NUM_ELEMENTS 10
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define BLOCK_SIZE 16
#define LOADING_DURATION 10  // seconds
#define NUM_MENU_OPTIONS 2
#define SPHERE_RADIUS 5
#define SPHERE_HOVER_HEIGHT 1.6f  // 0.1 * BLOCK_SIZE
#define SPHERE_HOVER_SPEED 0.05f
#define CAMERA_FOV 60.0f
#define CAMERA_NEAR 0.1f
#define CAMERA_FAR 1000.0f
#define CAMERA_ANIMATION_DURATION 5.0f  // seconds
#define ISO_SCALE 1.0f
#define PLAYER_MOVE_SPEED 0.1f
#define LIGHT_DIRECTION_X 0.0f
#define LIGHT_DIRECTION_Y 0.0f
#define LIGHT_DIRECTION_Z -1.0f
#define AMBIENT_LIGHT 0.5f
#define DIFFUSE_LIGHT 0.5f
#define MINIMAP_SIZE 100
#define MINIMAP_BORDER 1

enum Elements { AIR, BEDROCK, STONE, GRAVEL, SAND, SOIL, WATER, MAGMA, STEAM, ICE, METAL, DIRT, GRASS };
enum GameState { MAIN_MENU, NEW_WORLD, LOAD_GAME, LOADING, WORLD_VIEW, CAMERA_ANIMATION };
enum CameraMode { FIRST_PERSON, THIRD_PERSON, ISOMETRIC };

typedef struct {
  float x, y, z;
  float pitch, yaw;
} Camera;

typedef struct {
  float x, y, z;
} Player;

int world[WORLD_SIZE][WORLD_SIZE][WORLD_SIZE];
SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
TTF_Font* font = NULL;
enum GameState current_state = MAIN_MENU;
enum CameraMode camera_mode = ISOMETRIC;
time_t loading_start_time;
int selected_menu_item = 0;
Camera camera;
time_t animation_start_time;
Player player;
int seed = 0;

// Color definitions for each element
SDL_Color element_colors[NUM_ELEMENTS] = {
  {0, 0, 0, 0},           // AIR (transparent)
  {32, 32, 32, 255},      // BEDROCK (very dark gray)
  {128, 128, 128, 255},   // STONE (medium gray)
  {139, 69, 19, 255},     // GRAVEL (brown)
  {194, 178, 128, 255},   // SAND (sand color)
  {139, 69, 19, 255},     // SOIL (dark brown)
  {0, 0, 255, 255},       // WATER (blue)
  {255, 0, 0, 255},       // MAGMA (red)
  {200, 200, 200, 255},   // STEAM (light gray)
  {200, 200, 255, 255},   // ICE (light blue)
  {192, 192, 192, 255},   // METAL (silver)
  {139, 69, 19, 255},     // DIRT (brown)
  {0, 200, 0, 255}        // GRASS (bright green)
};

double perlin_noise(int x, int y, int z, int seed) {
  unsigned int hash = 0;
  hash = (hash * 73856093) ^ (x & 0xffffffff);
  hash = (hash * 19349663) ^ (y & 0xffffffff);
  hash = (hash * 83492791) ^ (z & 0xffffffff);
  hash = (hash * 1376312589) ^ (seed & 0xffffffff);
  return ((hash & 0x7fffffff) / (double)0x7fffffff);
}

void generate_world(int seed) {
  // Initialize the world with air
  for (int x = 0; x < WORLD_SIZE; x++)
    for (int y = 0; y < WORLD_SIZE; y++)
      for (int z = 0; z < WORLD_SIZE; z++)
        world[x][y][z] = AIR;

  // Create base bedrock layer
  for (int x = 0; x < WORLD_SIZE; x++)
    for (int y = 0; y < WORLD_SIZE; y++)
      world[x][y][0] = BEDROCK;

  // Generate terrain using multiple octaves of noise
  for (int x = 0; x < WORLD_SIZE; x++) {
    for (int y = 0; y < WORLD_SIZE; y++) {
      // Create height map using multiple octaves of noise
      float height = 0;
      float amplitude = 1.0f;
      float frequency = 0.1f;  // Lower frequency for gentler hills
      
      for (int i = 0; i < 3; i++) {
        height += amplitude * perlin_noise(x * frequency, y * frequency, 0, seed + i);
        amplitude *= 0.5f;
        frequency *= 2.0f;
      }
      
      // Scale and offset the height to create gentle rolling hills
      int terrain_height = (int)((height + 1.0f) * 0.5f * 8) + 4;  // Height range: 4-12
      
      // Fill layers with different materials
      for (int z = 1; z < terrain_height; z++) {
        if (z == 1) {
          // Second layer is always bedrock
          world[x][y][z] = BEDROCK;
        } else if (z < terrain_height - 4) {
          // Deep layers are mostly stone
          world[x][y][z] = STONE;
        } else if (z < terrain_height - 3) {
          // Transition to gravel
          world[x][y][z] = GRAVEL;
        } else if (z < terrain_height - 2) {
          // Transition to sand
          world[x][y][z] = SAND;
        } else if (z < terrain_height - 1) {
          // Transition to soil
          world[x][y][z] = SOIL;
        } else {
          // Top layer is always grass
          world[x][y][z] = GRASS;
        }
      }
    }
  }
}

void save_world(const char* filename) {
    FILE* file = fopen(filename, "wb");
    fwrite(world, sizeof(int), WORLD_SIZE * WORLD_SIZE * WORLD_SIZE, file);
    fclose(file);
}

void load_world(const char* filename) {
    FILE* file = fopen(filename, "rb");
    fread(world, sizeof(int), WORLD_SIZE * WORLD_SIZE * WORLD_SIZE, file);
    fclose(file);
}

void init_camera_animation() {
  // Start camera high above the sphere
  camera.x = WORLD_SIZE / 2;
  camera.y = WORLD_SIZE / 2;
  camera.z = WORLD_SIZE * 2;
  camera.pitch = -M_PI/2;  // Looking straight down
  camera.yaw = 0;
  animation_start_time = time(NULL);
  
  // Debug: Print initial camera state
  printf("Initial camera position: (%.2f, %.2f, %.2f)\n", camera.x, camera.y, camera.z);
  printf("Initial camera orientation: pitch=%.2f, yaw=%.2f\n", camera.pitch, camera.yaw);
}

void update_camera_animation() {
  time_t current_time = time(NULL);
  float t = difftime(current_time, animation_start_time) / CAMERA_ANIMATION_DURATION;

  if (t >= 1.0f) {
    current_state = WORLD_VIEW;
    return;
  }

  // Smooth easing function
  t = t * t * (3 - 2 * t);

  // Camera position animation
  float start_z = WORLD_SIZE * 2;
  float end_z = SPHERE_RADIUS * 2;
  camera.z = start_z + (end_z - start_z) * t;

  // Camera orientation animation
  float start_pitch = -M_PI/2;
  float end_pitch = 0;
  camera.pitch = start_pitch + (end_pitch - start_pitch) * t;
}

void project_point_isometric(float world_x, float world_y, float world_z, int* screen_x, int* screen_y) {
  // Convert to isometric coordinates with increased scale
  float iso_x = (world_x - world_y) * ISO_SCALE * BLOCK_SIZE;
  float iso_y = (world_x + world_y) * ISO_SCALE * BLOCK_SIZE * 0.5f - world_z * BLOCK_SIZE;

  // Center on screen
  *screen_x = WINDOW_WIDTH/2 + (int)iso_x;
  *screen_y = WINDOW_HEIGHT/2 + (int)iso_y;
}

void project_point(float world_x, float world_y, float world_z, int* screen_x, int* screen_y) {
  if (camera_mode == ISOMETRIC) {
    project_point_isometric(world_x, world_y, world_z, screen_x, screen_y);
    return;
  }

  // Convert world coordinates to camera space
  float dx = world_x - camera.x;
  float dy = world_y - camera.y;
  float dz = world_z - camera.z;

  // Apply camera rotation
  float cos_pitch = cos(camera.pitch);
  float sin_pitch = sin(camera.pitch);
  float cos_yaw = cos(camera.yaw);
  float sin_yaw = sin(camera.yaw);

  float x = dx * cos_yaw + dz * sin_yaw;
  float y = dy * cos_pitch + (-dx * sin_yaw + dz * cos_yaw) * sin_pitch;
  float z = -dy * sin_pitch + (-dx * sin_yaw + dz * cos_yaw) * cos_pitch;

  // Debug: Print projection calculations
  printf("Projecting point (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)\n", 
         world_x, world_y, world_z, x, y, z);

  // Perspective projection
  if (z > CAMERA_NEAR) {
    float scale = WINDOW_WIDTH / (2 * tan(CAMERA_FOV * M_PI / 360));
    *screen_x = WINDOW_WIDTH/2 + (int)(x * scale / z);
    *screen_y = WINDOW_HEIGHT/2 + (int)(y * scale / z);
  } else {
    *screen_x = -1;
    *screen_y = -1;
  }
}

float calculate_lighting(float nx, float ny, float nz) {
  // Normalize light direction
  float length = sqrt(LIGHT_DIRECTION_X * LIGHT_DIRECTION_X +
                     LIGHT_DIRECTION_Y * LIGHT_DIRECTION_Y +
                     LIGHT_DIRECTION_Z * LIGHT_DIRECTION_Z);
  float lx = LIGHT_DIRECTION_X / length;
  float ly = LIGHT_DIRECTION_Y / length;
  float lz = LIGHT_DIRECTION_Z / length;

  // Calculate dot product for diffuse lighting
  float dot = nx * lx + ny * ly + nz * lz;
  if (dot < 0) dot = 0;

  // Combine ambient and diffuse lighting with a minimum brightness
  float lighting = AMBIENT_LIGHT + DIFFUSE_LIGHT * dot;
  return lighting > 0.3f ? lighting : 0.3f;  // Ensure minimum brightness
}

void init_player() {
  player.x = WORLD_SIZE / 2;
  player.y = WORLD_SIZE / 2;
  
  // Find the highest non-air block at the starting position
  for (int z = WORLD_SIZE - 1; z >= 0; z--) {
    if (world[(int)player.x][(int)player.y][z] != AIR) {
      player.z = z + 1;  // Start one block above the highest solid block
      break;
    }
  }
}

void update_player() {
  const Uint8* keystate = SDL_GetKeyboardState(NULL);
  float move_x = 0.0f;
  float move_y = 0.0f;

  // Handle WASD movement
  if (keystate[SDL_SCANCODE_W]) move_y -= PLAYER_MOVE_SPEED;
  if (keystate[SDL_SCANCODE_S]) move_y += PLAYER_MOVE_SPEED;
  if (keystate[SDL_SCANCODE_A]) move_x -= PLAYER_MOVE_SPEED;
  if (keystate[SDL_SCANCODE_D]) move_x += PLAYER_MOVE_SPEED;

  // Calculate potential new position
  float new_x = player.x + move_x;
  float new_y = player.y + move_y;

  // Clamp to world bounds
  if (new_x < 0) new_x = 0;
  if (new_x >= WORLD_SIZE) new_x = WORLD_SIZE - 1;
  if (new_y < 0) new_y = 0;
  if (new_y >= WORLD_SIZE) new_y = WORLD_SIZE - 1;

  // Check for collision with solid blocks
  int player_z = (int)player.z;
  if (world[(int)new_x][(int)new_y][player_z] == AIR) {
    // Only update position if the new position is air
    player.x = new_x;
    player.y = new_y;
  }

  // Apply gravity
  int current_x = (int)player.x;
  int current_y = (int)player.y;
  int current_z = (int)player.z;

  // Check if there's a block below the player
  if (current_z > 0 && world[current_x][current_y][current_z - 1] == AIR) {
    // Fall down if there's no block below
    player.z -= 0.1f;  // Fall speed
  } else {
    // Stop falling when hitting a block
    player.z = current_z;
  }
}

void render_minimap() {
  // Calculate minimap position and size (top right corner)
  int map_x = WINDOW_WIDTH - MINIMAP_SIZE - MINIMAP_BORDER;
  int map_y = MINIMAP_BORDER;
  int map_size = MINIMAP_SIZE - 2 * MINIMAP_BORDER;
  
  // Draw white border
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_Rect border = {map_x - MINIMAP_BORDER, map_y - MINIMAP_BORDER, 
                    map_size + 2 * MINIMAP_BORDER, map_size + 2 * MINIMAP_BORDER};
  SDL_RenderDrawRect(renderer, &border);
  
  // Draw black background
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_Rect background = {map_x, map_y, map_size, map_size};
  SDL_RenderFillRect(renderer, &background);
  
  // Calculate scale factor for world to minimap
  float scale = (float)map_size / WORLD_SIZE;
  
  // Draw terrain
  for (int x = 0; x < WORLD_SIZE; x++) {
    for (int y = 0; y < WORLD_SIZE; y++) {
      // Find the highest non-air block at this x,y position
      int highest_z = 0;
      for (int z = 0; z < WORLD_SIZE; z++) {
        if (world[x][y][z] != AIR) {
          highest_z = z;
        }
      }
      
      if (highest_z > 0) {
        // Get the color of the top block
        SDL_Color color = element_colors[world[x][y][highest_z]];
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        
        // Draw the block on the minimap
        SDL_Rect block = {
          map_x + (int)(x * scale),
          map_y + (int)(y * scale),
          (int)ceil(scale),
          (int)ceil(scale)
        };
        SDL_RenderFillRect(renderer, &block);
        
        // Draw grid lines
        SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
        SDL_RenderDrawRect(renderer, &block);
      }
    }
  }
  
  // Draw player position as a red cross
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  int player_x = map_x + (int)(player.x * scale);
  int player_y = map_y + (int)(player.y * scale);
  int cross_size = (int)ceil(scale) * 2;
  
  // Draw horizontal line
  SDL_RenderDrawLine(renderer, 
    player_x - cross_size/2, player_y,
    player_x + cross_size/2, player_y);
  
  // Draw vertical line
  SDL_RenderDrawLine(renderer,
    player_x, player_y - cross_size/2,
    player_x, player_y + cross_size/2);
}

void render_position_info() {
  // Get the block type the player is in
  int current_block = world[(int)player.x][(int)player.y][(int)player.z];
  
  // Get the block type the player is standing on
  int standing_block = AIR;
  for (int z = (int)player.z; z >= 0; z--) {
    if (world[(int)player.x][(int)player.y][z] != AIR) {
      standing_block = world[(int)player.x][(int)player.y][z];
      break;
    }
  }
  
  // Create position string
  char pos_str[128];
  const char* block_names[] = {"AIR", "BEDROCK", "STONE", "GRAVEL", "SAND", "SOIL", 
                              "WATER", "MAGMA", "STEAM", "ICE", "METAL", "DIRT", "GRASS"};
  snprintf(pos_str, sizeof(pos_str), 
           "Pos: (%.1f, %.1f, %.1f)\nCurrent: %s\nStanding on: %s", 
           player.x, player.y, player.z, 
           block_names[current_block], 
           block_names[standing_block]);
  
  // Render the text
  SDL_Color white = {255, 255, 255, 255};
  SDL_Surface* surface = TTF_RenderText_Solid(font, pos_str, white);
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_Rect rect = {10, 10, surface->w, surface->h};
  SDL_RenderCopy(renderer, texture, NULL, &rect);
  SDL_FreeSurface(surface);
  SDL_DestroyTexture(texture);
}

void render_world() {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  // Calculate the viewing distance
  float max_dist = sqrt(WORLD_SIZE * WORLD_SIZE * 3);  // Diagonal of the world

  // Render blocks from back to front
  for (float dist = max_dist; dist >= 0; dist -= 1.0f) {
    // Calculate the current slice of the world to render
    int min_x = (int)(camera.x - dist);
    int max_x = (int)(camera.x + dist);
    int min_y = (int)(camera.y - dist);
    int max_y = (int)(camera.y + dist);
    int min_z = (int)(camera.z - dist);
    int max_z = (int)(camera.z + dist);

    // Clamp to world bounds
    min_x = min_x < 0 ? 0 : min_x;
    max_x = max_x >= WORLD_SIZE ? WORLD_SIZE - 1 : max_x;
    min_y = min_y < 0 ? 0 : min_y;
    max_y = max_y >= WORLD_SIZE ? WORLD_SIZE - 1 : max_y;
    min_z = min_z < 0 ? 0 : min_z;
    max_z = max_z >= WORLD_SIZE ? WORLD_SIZE - 1 : max_z;

    // Render blocks in this slice
    for (int x = min_x; x <= max_x; x++) {
      for (int y = min_y; y <= max_y; y++) {
        for (int z = min_z; z <= max_z; z++) {
          // Only render non-air blocks
          if (world[x][y][z] != AIR) {
            // Calculate distance from camera
            float dx = x - camera.x;
            float dy = y - camera.y;
            float dz = z - camera.z;
            float block_dist = sqrt(dx*dx + dy*dy + dz*dz);

            // Only render if this block is at the current distance
            if (fabs(block_dist - dist) < 1.0f) {
              // Check if block is visible (has air adjacent)
              int is_visible = 0;
              if (x == 0 || x == WORLD_SIZE - 1 || 
                  y == 0 || y == WORLD_SIZE - 1 ||
                  z == 0 || z == WORLD_SIZE - 1) {
                is_visible = 1;
              } else if (world[x-1][y][z] == AIR || world[x+1][y][z] == AIR ||
                        world[x][y-1][z] == AIR || world[x][y+1][z] == AIR ||
                        world[x][y][z-1] == AIR || world[x][y][z+1] == AIR) {
                is_visible = 1;
              }

              if (is_visible) {
                SDL_Color color = element_colors[world[x][y][z]];
                
                // Calculate screen position
                int screen_x, screen_y;
                project_point(x, y, z, &screen_x, &screen_y);
                if (screen_x >= 0 && screen_x < WINDOW_WIDTH && 
                    screen_y >= 0 && screen_y < WINDOW_HEIGHT) {
                  
                  // Draw the block
                  SDL_Rect block = {
                    screen_x - BLOCK_SIZE/2,
                    screen_y - BLOCK_SIZE/2,
                    BLOCK_SIZE,
                    BLOCK_SIZE
                  };
                  
                  // Draw the main block color
                  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                  SDL_RenderFillRect(renderer, &block);
                  
                  // Draw darker borders
                  SDL_SetRenderDrawColor(renderer, 
                    color.r * 0.7f, color.g * 0.7f, color.b * 0.7f, color.a);
                  
                  // Draw top border
                  SDL_RenderDrawLine(renderer, 
                    block.x, block.y,
                    block.x + block.w, block.y);
                  
                  // Draw left border
                  SDL_RenderDrawLine(renderer,
                    block.x, block.y,
                    block.x, block.y + block.h);
                  
                  // Draw right border
                  SDL_RenderDrawLine(renderer,
                    block.x + block.w, block.y,
                    block.x + block.w, block.y + block.h);
                  
                  // Draw bottom border
                  SDL_RenderDrawLine(renderer,
                    block.x, block.y + block.h,
                    block.x + block.w, block.y + block.h);
                }
              }
            }
          }
        }
      }
    }
  }

  // Render the player sphere
  int screen_x, screen_y;
  project_point(player.x, player.y, player.z, &screen_x, &screen_y);
  if (screen_x >= 0 && screen_x < WINDOW_WIDTH && 
      screen_y >= 0 && screen_y < WINDOW_HEIGHT) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    if (camera_mode == ISOMETRIC) {
      // Draw a diamond shape for the sphere in isometric view
      SDL_Point points[4] = {
        {screen_x, screen_y - SPHERE_RADIUS},
        {screen_x + SPHERE_RADIUS, screen_y},
        {screen_x, screen_y + SPHERE_RADIUS},
        {screen_x - SPHERE_RADIUS, screen_y}
      };
      SDL_RenderDrawLines(renderer, points, 4);
      SDL_RenderDrawLine(renderer, points[0].x, points[0].y, points[2].x, points[2].y);
    } else {
      for (int x = -SPHERE_RADIUS; x <= SPHERE_RADIUS; x++) {
        for (int y = -SPHERE_RADIUS; y <= SPHERE_RADIUS; y++) {
          if (x*x + y*y <= SPHERE_RADIUS*SPHERE_RADIUS) {
            SDL_RenderDrawPoint(renderer, screen_x + x, screen_y + y);
          }
        }
      }
    }
  }

  // Render the minimap and position info
  render_minimap();
  render_position_info();

  SDL_RenderPresent(renderer);
}

void render_menu() {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  SDL_Color white = {255, 255, 255, 255};
  SDL_Surface* surface;
  SDL_Texture* texture;
  SDL_Rect rect;

  // Title
  surface = TTF_RenderText_Solid(font, "VERSE", white);
  texture = SDL_CreateTextureFromSurface(renderer, surface);
  rect = (SDL_Rect){WINDOW_WIDTH/2 - surface->w/2, 100, surface->w, surface->h};
  SDL_RenderCopy(renderer, texture, NULL, &rect);
  SDL_FreeSurface(surface);
  SDL_DestroyTexture(texture);

  // Menu options
  const char* options[] = {"New World", "Load Game"};
  for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
    surface = TTF_RenderText_Solid(font, options[i], white);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    rect = (SDL_Rect){WINDOW_WIDTH/2 - surface->w/2, 200 + i*50, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);

    // Draw selection triangle if this is the selected item
    if (i == selected_menu_item) {
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
      int text_center_x = WINDOW_WIDTH/2 - surface->w/2;
      int text_center_y = 200 + i*50 + surface->h/2;
      
      // Triangle points (pointing right towards the text)
      SDL_Point triangle[3] = {
        {text_center_x - 30, text_center_y},          // Left point
        {text_center_x - 20, text_center_y - 10},     // Top right point
        {text_center_x - 20, text_center_y + 10}      // Bottom right point
      };
      
      // Draw the triangle
      SDL_RenderDrawLines(renderer, triangle, 3);
      SDL_RenderDrawLine(renderer, triangle[0].x, triangle[0].y, triangle[2].x, triangle[2].y);
    }
  }

  SDL_RenderPresent(renderer);
}

void render_loading_screen() {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  SDL_Color white = {255, 255, 255, 255};
  SDL_Surface* surface;
  SDL_Texture* texture;
  SDL_Rect rect;

  // Loading text
  surface = TTF_RenderText_Solid(font, "Generating World...", white);
  texture = SDL_CreateTextureFromSurface(renderer, surface);
  rect = (SDL_Rect){WINDOW_WIDTH/2 - surface->w/2, WINDOW_HEIGHT/2 - surface->h/2, surface->w, surface->h};
  SDL_RenderCopy(renderer, texture, NULL, &rect);
  SDL_FreeSurface(surface);
  SDL_DestroyTexture(texture);

  // Progress bar background
  SDL_Rect progress_bg = {WINDOW_WIDTH/4, WINDOW_HEIGHT/2 + 50, WINDOW_WIDTH/2, 20};
  SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
  SDL_RenderFillRect(renderer, &progress_bg);

  // Progress bar
  time_t current_time = time(NULL);
  double progress = difftime(current_time, loading_start_time) / LOADING_DURATION;
  if (progress > 1.0) progress = 1.0;
  
  SDL_Rect progress_bar = {WINDOW_WIDTH/4, WINDOW_HEIGHT/2 + 50, (int)(WINDOW_WIDTH/2 * progress), 20};
  SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
  SDL_RenderFillRect(renderer, &progress_bar);

  SDL_RenderPresent(renderer);
}

void handle_menu_input(SDL_Event* event) {
  if (event->type == SDL_MOUSEBUTTONDOWN) {
    int x, y;
    SDL_GetMouseState(&x, &y);
    
    // Check if click is within any menu option
    for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
      if (y >= 200 + i*50 && y < 200 + (i+1)*50) {
        selected_menu_item = i;
        if (i == 0) {
          current_state = LOADING;
          loading_start_time = time(NULL);
        } else if (i == 1) {
          current_state = LOAD_GAME;
        }
        break;
      }
    }
  } else if (event->type == SDL_KEYDOWN) {
    switch (event->key.keysym.sym) {
      case SDLK_UP:
        selected_menu_item = (selected_menu_item - 1 + NUM_MENU_OPTIONS) % NUM_MENU_OPTIONS;
        break;
      case SDLK_DOWN:
        selected_menu_item = (selected_menu_item + 1) % NUM_MENU_OPTIONS;
        break;
      case SDLK_RETURN:
        if (selected_menu_item == 0) {
          current_state = LOADING;
          loading_start_time = time(NULL);
        } else if (selected_menu_item == 1) {
          current_state = LOAD_GAME;
        }
        break;
    }
  }
}

void handle_world_input(SDL_Event* event) {
  if (event->type == SDL_KEYDOWN) {
    switch (event->key.keysym.sym) {
      case SDLK_TAB:
        // Cycle through camera modes
        camera_mode = (camera_mode + 1) % 3;
        if (camera_mode == ISOMETRIC) {
          // Reset camera for isometric view
          camera.x = WORLD_SIZE / 2;
          camera.y = WORLD_SIZE / 2;
          camera.z = WORLD_SIZE / 2;
          camera.pitch = -M_PI/4;  // 45-degree angle
          camera.yaw = M_PI/4;     // 45-degree angle
        }
        break;
    }
  }
}

void init_gui() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    exit(1);
  }

  if (TTF_Init() < 0) {
    printf("TTF could not initialize! TTF_Error: %s\n", TTF_GetError());
    SDL_Quit();
    exit(1);
  }

  window = SDL_CreateWindow("Verse World", 
                           SDL_WINDOWPOS_UNDEFINED, 
                           SDL_WINDOWPOS_UNDEFINED, 
                           WINDOW_WIDTH, 
                           WINDOW_HEIGHT, 
                           SDL_WINDOW_SHOWN);
  
  if (window == NULL) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_Quit();
    exit(1);
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL) {
    printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    exit(1);
  }

  // Use system font on macOS
  font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 24);
  if (font == NULL) {
    printf("Failed to load font! TTF_Error: %s\n", TTF_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    exit(1);
  }

  // Set default camera mode to isometric
  camera_mode = ISOMETRIC;
  camera.x = WORLD_SIZE / 2;
  camera.y = WORLD_SIZE / 2;
  camera.z = WORLD_SIZE / 2;
  camera.pitch = -M_PI/4;  // 45-degree angle
  camera.yaw = M_PI/4;     // 45-degree angle
}

void cleanup() {
  if (font) TTF_CloseFont(font);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
}

void handle_state() {
  switch (current_state) {
    case MAIN_MENU:
      render_menu();
      break;
    case NEW_WORLD:
      current_state = LOADING;
      loading_start_time = time(NULL);
      break;
    case LOADING:
      render_loading_screen();
      time_t current_time = time(NULL);
      if (difftime(current_time, loading_start_time) >= LOADING_DURATION) {
        // Generate new world with the provided seed
        generate_world(seed);
        current_state = CAMERA_ANIMATION;
        init_camera_animation();
        init_player();
      }
      break;
    case CAMERA_ANIMATION:
      update_camera_animation();
      render_world();
      break;
    case WORLD_VIEW:
      update_player();
      render_world();
      break;
    case LOAD_GAME:
      // TODO: Implement game loading
      break;
  }
}

int main(int argc, char** argv) {
  init_gui();
  init_player();

  // Parse command line arguments
  if (argc > 1) {
    seed = atoi(argv[1]);
    printf("Using seed: %d\n", seed);
  } else {
    printf("No seed provided, using current time: %d\n", seed);
  }

  // Main game loop
  int running = 1;
  SDL_Event event;

  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      }
      
      if (current_state == MAIN_MENU) {
        handle_menu_input(&event);
      } else if (current_state == WORLD_VIEW || current_state == CAMERA_ANIMATION) {
        handle_world_input(&event);
      }
    }

    handle_state();

    SDL_Delay(16); // Cap at ~60 FPS
  }

  cleanup();
  return 0;
}
