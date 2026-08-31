#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define STARFIGHTER_WIDTH 800
#define STARFIGHTER_HEIGHT 600
#define MAX_STARS 1000


// Game state
typedef struct {
    float x, y, z;
    float yaw, pitch;
    float velocity_x, velocity_y, velocity_z;
    float health, energy;
} Starfighter;

typedef struct {
    float x, y, z;
} Star;

typedef struct {
    SDL_Window *window;
    SDL_GLContext gl_context;
    TTF_Font *font;
    Starfighter player_ship;
    Star stars[MAX_STARS];
    int running;
    int mouse_locked;
    int show_menu;
    int menu_selection;
} StarfighterGame;

// Input handler
typedef struct {
    int keys[SDL_NUM_SCANCODES];
    int prev_keys[SDL_NUM_SCANCODES];
    int mouse_buttons[5];
    int prev_mouse_buttons[5];
    int mouse_x, mouse_y;
    int prev_mouse_x, prev_mouse_y;
} InputHandler;

// Custom gluPerspective implementation
void starfighter_gluPerspective(float fovy, float aspect, float zNear, float zFar) {
    float f = 1.0f / tanf(fovy * M_PI / 360.0f);
    float matrix[16] = {
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, (zFar + zNear) / (zNear - zFar), -1.0f,
        0.0f, 0.0f, (2.0f * zFar * zNear) / (zNear - zFar), 0.0f
    };
    glLoadMatrixf(matrix);
}

// Custom gluLookAt implementation
void starfighter_gluLookAt(float eyeX, float eyeY, float eyeZ,
                          float centerX, float centerY, float centerZ,
                          float upX, float upY, float upZ) {
    float forward[3] = {centerX - eyeX, centerY - eyeY, centerZ - eyeZ};
    float up[3] = {upX, upY, upZ};

    // Normalize forward vector
    float length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    forward[0] /= length;
    forward[1] /= length;
    forward[2] /= length;

    // Normalize up vector
    length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    up[0] /= length;
    up[1] /= length;
    up[2] /= length;

    // Calculate right vector
    float right[3] = {
        forward[1] * up[2] - forward[2] * up[1],
        forward[2] * up[0] - forward[0] * up[2],
        forward[0] * up[1] - forward[1] * up[0]
    };

    // Create view matrix
    float matrix[16] = {
        right[0], up[0], -forward[0], 0.0f,
        right[1], up[1], -forward[1], 0.0f,
        right[2], up[2], -forward[2], 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    glMultMatrixf(matrix);
    glTranslatef(-eyeX, -eyeY, -eyeZ);
}

// Initialize the game
int starfighter_init(StarfighterGame *game) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL init failed: %s\n", SDL_GetError());
        return 0;
    }

    if (TTF_Init() < 0) {
        printf("TTF init failed: %s\n", SDL_GetError());
        return 0;
    }

    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // Create window with OpenGL support
    game->window = SDL_CreateWindow(
        "Starfighter",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        STARFIGHTER_WIDTH, STARFIGHTER_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );

    if (!game->window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        return 0;
    }

    // Create OpenGL context
    game->gl_context = SDL_GL_CreateContext(game->window);
    if (!game->gl_context) {
        printf("OpenGL context creation failed: %s\n", SDL_GetError());
        return 0;
    }

    printf("OpenGL context created successfully!\n");
    printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));

    // Load font
    game->font = TTF_OpenFont("assets/fonts/visitor-tt2-brk.ttf", 24);
    if (!game->font) {
        game->font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 24);
        if (!game->font) {
            printf("Font loading failed\n");
        }
    }

    // Initialize game state
    game->running = 1;
    game->mouse_locked = 0;
    game->show_menu = 1;
    game->menu_selection = 0;

    // Initialize player ship
    game->player_ship.x = 0.0f;
    game->player_ship.y = -50.0f;  // Closer to bottom
    game->player_ship.z = 0.0f;
    game->player_ship.yaw = 0.0f;
    game->player_ship.pitch = 0.0f;
    game->player_ship.velocity_x = 0.0f;
    game->player_ship.velocity_y = 0.0f;
    game->player_ship.velocity_z = 0.0f;
    game->player_ship.health = 100.0f;
    game->player_ship.energy = 100.0f;

    // Generate star field
    for (int i = 0; i < MAX_STARS; i++) {
        game->stars[i].x = (float)(rand() % 2000 - 1000);
        game->stars[i].y = (float)(rand() % 2000 - 1000);
        game->stars[i].z = (float)(rand() % 2000 - 1000);
    }

    return 1;
}

// Handle input
void starfighter_handle_input(StarfighterGame *game, InputHandler *input) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                game->running = 0;
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (game->mouse_locked) {
                        game->mouse_locked = 0;
                        game->show_menu = 1;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    } else if (game->show_menu) {
                        game->running = 0;
                    }
                }

                if (game->show_menu) {
                    switch (event.key.keysym.sym) {
                        case SDLK_UP:
                            game->menu_selection = (game->menu_selection - 1 + 3) % 3;
                            break;
                        case SDLK_DOWN:
                            game->menu_selection = (game->menu_selection + 1) % 3;
                            break;
                        case SDLK_RETURN:
                            if (game->menu_selection == 0) { // New Game
                                game->show_menu = 0;
                                game->mouse_locked = 1;
                                SDL_SetRelativeMouseMode(SDL_TRUE);
                            } else if (game->menu_selection == 2) { // Exit
                                game->running = 0;
                            }
                            break;
                    }
                } else {
                    // Game controls
                    switch (event.key.keysym.sym) {
                        case SDLK_w:
                            game->player_ship.velocity_z = -5.0f;
                            break;
                        case SDLK_s:
                            game->player_ship.velocity_z = 5.0f;
                            break;
                        case SDLK_a:
                            game->player_ship.velocity_x = -5.0f;
                            break;
                        case SDLK_d:
                            game->player_ship.velocity_x = 5.0f;
                            break;
                        case SDLK_SPACE:
                            game->player_ship.velocity_y = 5.0f;
                            break;
                        case SDLK_LSHIFT:
                            game->player_ship.velocity_y = -5.0f;
                            break;
                    }
                }
                break;

            case SDL_KEYUP:
                if (!game->show_menu) {
                    switch (event.key.keysym.sym) {
                        case SDLK_w:
                        case SDLK_s:
                            game->player_ship.velocity_z = 0.0f;
                            break;
                        case SDLK_a:
                        case SDLK_d:
                            game->player_ship.velocity_x = 0.0f;
                            break;
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                            game->player_ship.velocity_y = 0.0f;
                            break;
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (game->mouse_locked && !game->show_menu) {
                    float sensitivity = 0.01f;
                    game->player_ship.yaw += event.motion.xrel * sensitivity;
                    game->player_ship.pitch -= event.motion.yrel * sensitivity;

                    // Clamp pitch
                    if (game->player_ship.pitch > M_PI / 2.0f) game->player_ship.pitch = M_PI / 2.0f;
                    if (game->player_ship.pitch < -M_PI / 2.0f) game->player_ship.pitch = -M_PI / 2.0f;
                }
                break;
        }
    }
}

// Update game state
void starfighter_update(StarfighterGame *game) {
    if (game->show_menu) return;

    // Update ship position
    game->player_ship.x += game->player_ship.velocity_x;
    game->player_ship.y += game->player_ship.velocity_y;
    game->player_ship.z += game->player_ship.velocity_z;

    // Simple bounds checking
    if (game->player_ship.x > 500.0f) game->player_ship.x = 500.0f;
    if (game->player_ship.x < -500.0f) game->player_ship.x = -500.0f;
    if (game->player_ship.y > 500.0f) game->player_ship.y = 500.0f;
    if (game->player_ship.y < -500.0f) game->player_ship.y = -500.0f;
    if (game->player_ship.z > 500.0f) game->player_ship.z = 500.0f;
    if (game->player_ship.z < -500.0f) game->player_ship.z = -500.0f;
}

// Render 3D stars
void starfighter_render_stars_3d(StarfighterGame *game) {
    glColor3f(1.0f, 1.0f, 1.0f);  // White stars
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < MAX_STARS; i += 10) { // Draw every 10th star for performance
        glVertex3f(game->stars[i].x, game->stars[i].y, game->stars[i].z);
    }
    glEnd();
}

// Render 3D ship
void starfighter_render_ship_3d(Starfighter *ship) {
    glPushMatrix();
    glTranslatef(ship->x, ship->y, ship->z);
    glRotatef(ship->yaw * 180.0f / M_PI, 0.0f, 1.0f, 0.0f);
    glRotatef(ship->pitch * 180.0f / M_PI, 1.0f, 0.0f, 0.0f);

    // Draw ship body (pyramid)
    glColor3f(1.0f, 1.0f, 1.0f);  // White
    glBegin(GL_TRIANGLES);
    // Front face
    glVertex3f(0.0f, 10.0f, 20.0f);   // Top
    glVertex3f(-16.0f, -10.0f, -20.0f); // Bottom left
    glVertex3f(16.0f, -10.0f, -20.0f);  // Bottom right

    // Left face
    glVertex3f(0.0f, 10.0f, 20.0f);   // Top
    glVertex3f(-16.0f, -10.0f, -20.0f); // Bottom left
    glVertex3f(0.0f, -10.0f, 20.0f);   // Bottom center

    // Right face
    glVertex3f(0.0f, 10.0f, 20.0f);   // Top
    glVertex3f(16.0f, -10.0f, -20.0f);  // Bottom right
    glVertex3f(0.0f, -10.0f, 20.0f);   // Bottom center

    // Bottom face
    glVertex3f(-16.0f, -10.0f, -20.0f); // Bottom left
    glVertex3f(16.0f, -10.0f, -20.0f);  // Bottom right
    glVertex3f(0.0f, -10.0f, 20.0f);   // Bottom center
    glEnd();

    // Draw engine glow
    glColor3f(0.0f, 1.0f, 1.0f);  // Cyan
    glBegin(GL_QUADS);
    glVertex3f(-8.0f, -10.0f, -20.0f);
    glVertex3f(8.0f, -10.0f, -20.0f);
    glVertex3f(8.0f, -10.0f, -30.0f);
    glVertex3f(-8.0f, -10.0f, -30.0f);
    glEnd();

    glPopMatrix();
}

// Render HUD using OpenGL
void starfighter_render_hud_opengl(StarfighterGame *game) {
    // Switch to 2D orthographic projection for HUD
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, STARFIGHTER_WIDTH, STARFIGHTER_HEIGHT, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    // Draw reticle in center
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    // Horizontal line
    glVertex2f(STARFIGHTER_WIDTH / 2 - 20, STARFIGHTER_HEIGHT / 2);
    glVertex2f(STARFIGHTER_WIDTH / 2 + 20, STARFIGHTER_HEIGHT / 2);
    // Vertical line
    glVertex2f(STARFIGHTER_WIDTH / 2, STARFIGHTER_HEIGHT / 2 - 20);
    glVertex2f(STARFIGHTER_WIDTH / 2, STARFIGHTER_HEIGHT / 2 + 20);
    glEnd();

    // Draw health bar
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(20, 20);
    glVertex2f(20 + (int)(200 * game->player_ship.health / 100.0f), 20);
    glVertex2f(20 + (int)(200 * game->player_ship.health / 100.0f), 40);
    glVertex2f(20, 40);
    glEnd();

    // Draw energy bar
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(20, 50);
    glVertex2f(20 + (int)(200 * game->player_ship.energy / 100.0f), 50);
    glVertex2f(20 + (int)(200 * game->player_ship.energy / 100.0f), 70);
    glVertex2f(20, 50);
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// Render menu using OpenGL
void starfighter_render_menu_opengl(StarfighterGame *game) {
    // Switch to 2D orthographic projection for menu
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, STARFIGHTER_WIDTH, STARFIGHTER_HEIGHT, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    // Draw menu background
    glColor3f(0.2f, 0.2f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(STARFIGHTER_WIDTH, 0);
    glVertex2f(STARFIGHTER_WIDTH, STARFIGHTER_HEIGHT);
    glVertex2f(0, STARFIGHTER_HEIGHT);
    glEnd();

    // Draw menu buttons
    for (int i = 0; i < 3; i++) {
        if (i == game->menu_selection) {
            glColor3f(1.0f, 1.0f, 1.0f);  // White for selected
        } else {
            glColor3f(0.5f, 0.5f, 0.5f);  // Gray for unselected
        }

        glBegin(GL_QUADS);
        glVertex2f(STARFIGHTER_WIDTH / 2 - 80, 220 + i * 60);
        glVertex2f(STARFIGHTER_WIDTH / 2 + 80, 220 + i * 60);
        glVertex2f(STARFIGHTER_WIDTH / 2 + 80, 260 + i * 60);
        glVertex2f(STARFIGHTER_WIDTH / 2 - 80, 260 + i * 60);
        glEnd();
    }

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// Main render function
void starfighter_render(StarfighterGame *game, int frame_count) {
    if (!game->window) return;

    // Use OpenGL for all rendering
    SDL_GL_MakeCurrent(game->window, game->gl_context);

    // Set up OpenGL viewport and clear buffers
    glViewport(0, 0, STARFIGHTER_WIDTH, STARFIGHTER_HEIGHT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Set up 3D projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    starfighter_gluPerspective(45.0f, (float)STARFIGHTER_WIDTH / (float)STARFIGHTER_HEIGHT, 0.1f, 1000.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Position camera behind and slightly above the ship
    float camera_distance = 30.0f;
    float camera_height = 15.0f;
    float camera_x = game->player_ship.x - sinf(game->player_ship.yaw) * camera_distance;
    float camera_z = game->player_ship.z - cosf(game->player_ship.yaw) * camera_distance;
    float camera_y = game->player_ship.y + camera_height;

    // Look at a point ahead of the ship
    float look_ahead = 15.0f;
    float look_x = game->player_ship.x + sinf(game->player_ship.yaw) * look_ahead;
    float look_z = game->player_ship.z + cosf(game->player_ship.yaw) * look_ahead;
    float look_y = game->player_ship.y;

    starfighter_gluLookAt(
        camera_x, camera_y, camera_z,
        look_x, look_y, look_z,
        0.0f, 1.0f, 0.0f
    );

    glEnable(GL_DEPTH_TEST);

    // Draw 3D world
    starfighter_render_stars_3d(game);
    starfighter_render_ship_3d(&game->player_ship);

    // Draw HUD and menu
    starfighter_render_hud_opengl(game);
    if (game->show_menu) {
        starfighter_render_menu_opengl(game);
    }

    // Check for OpenGL errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("OpenGL error: 0x%x\n", error);
    }

    // Debug output
    if (frame_count % 60 == 0) {
        printf("Ship pos: (%.1f, %.1f, %.1f) Yaw: %.1f Pitch: %.1f\n",
               game->player_ship.x, game->player_ship.y, game->player_ship.z,
               game->player_ship.yaw * 180.0f / M_PI, game->player_ship.pitch * 180.0f / M_PI);
    }

    SDL_GL_SwapWindow(game->window);
}

// Cleanup
void starfighter_cleanup(StarfighterGame *game) {
    if (game->gl_context) {
        SDL_GL_DeleteContext(game->gl_context);
    }
    if (game->window) {
        SDL_DestroyWindow(game->window);
    }
    if (game->font) {
        TTF_CloseFont(game->font);
    }
    TTF_Quit();
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    StarfighterGame game = {0};
    InputHandler input = {0};

    if (!starfighter_init(&game)) {
        printf("Failed to initialize game\n");
        return 1;
    }

    printf("Starfighter initialized successfully!\n");

    int frame_count = 0;
    while (game.running) {
        starfighter_handle_input(&game, &input);
        starfighter_update(&game);
        starfighter_render(&game, frame_count);

        frame_count++;
        SDL_Delay(16);  // ~60 FPS
    }

    starfighter_cleanup(&game);
    printf("Game closed successfully\n");

    return 0;
}
