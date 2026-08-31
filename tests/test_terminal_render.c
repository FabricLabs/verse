#include "terminal_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

// Test patterns for terminal rendering
void draw_test_pattern(TerminalRenderer* renderer, int frame) {
    terminal_render_clear(renderer);

    // Draw animated gradient
    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            // Create animated color patterns
            uint8_t r = (uint8_t)((sin(x * 0.1 + frame * 0.1) + 1) * 127);
            uint8_t g = (uint8_t)((sin(y * 0.1 + frame * 0.05) + 1) * 127);
            uint8_t b = (uint8_t)((sin((x + y) * 0.05 + frame * 0.02) + 1) * 127);

            terminal_render_set_pixel(renderer, x, y, r, g, b);
        }
    }

    // Draw moving circle
    int center_x = renderer->width / 2;
    int center_y = renderer->height / 2;
    int radius = 20;

    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            int dx = x - center_x;
            int dy = y - center_y;
            int distance = (int)sqrt(dx * dx + dy * dy);

            if (distance < radius) {
                // Draw white circle
                terminal_render_set_pixel(renderer, x, y, 255, 255, 255);
            }
        }
    }
}

void draw_game_simulation(TerminalRenderer* renderer, int frame) {
    terminal_render_clear(renderer);

    // Simulate a simple game world (256x240 like our game)
    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            // Sky gradient
            uint8_t sky_r = 100 + (y * 50) / renderer->height;
            uint8_t sky_g = 150 + (y * 100) / renderer->height;
            uint8_t sky_b = 255;

            // Ground (bottom third)
            if (y > renderer->height * 2 / 3) {
                uint8_t ground_r = 139;
                uint8_t ground_g = 69;
                uint8_t ground_b = 19;
                terminal_render_set_pixel(renderer, x, y, ground_r, ground_g, ground_b);
            } else {
                terminal_render_set_pixel(renderer, x, y, sky_r, sky_g, sky_b);
            }
        }
    }

    // Draw animated player (moving dot)
    int player_x = (renderer->width / 2) + (int)(sin(frame * 0.1) * 50);
    int player_y = renderer->height * 2 / 3 - 10;

    // Player body (red)
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int px = player_x + dx;
            int py = player_y + dy;
            if (px >= 0 && px < renderer->width && py >= 0 && py < renderer->height) {
                terminal_render_set_pixel(renderer, px, py, 255, 0, 0);
            }
        }
    }

    // Draw some "springs" (blue dots) - simulating our spring system
    for (int i = 0; i < 3; i++) {
        int spring_x = 50 + i * 60 + (int)(sin(frame * 0.05 + i) * 10);
        int spring_y = renderer->height * 2 / 3 - 5;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int px = spring_x + dx;
                int py = spring_y + dy;
                if (px >= 0 && px < renderer->width && py >= 0 && py < renderer->height) {
                    terminal_render_set_pixel(renderer, px, py, 0, 0, 255);
                }
            }
        }
    }
}

void draw_spring_world_demo(TerminalRenderer* renderer, int frame) {
    terminal_render_clear(renderer);

    // Simulate a RANDOM world with springs
    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            // Base terrain (farm-like)
            uint8_t r, g, b;

            if (y < renderer->height / 4) {
                // Sky
                r = 135; g = 206; b = 235;
            } else if (y < renderer->height * 3 / 4) {
                // Grass/soil
                r = 34; g = 139; b = 34;
            } else {
                // Bedrock
                r = 105; g = 105; b = 105;
            }

            terminal_render_set_pixel(renderer, x, y, r, g, b);
        }
    }

    // Draw springs at different rarity levels
    int springs[4][2] = {
        {64, 96},   // Level 0 (Common)
        {128, 96},  // Level 1 (Uncommon)
        {192, 96},  // Level 2 (Rare)
        {256, 96}   // Level 3 (Very Rare) - won't show in 256x240
    };

    for (int i = 0; i < 3; i++) { // Only show first 3 springs
        int spring_x = springs[i][0];
        int spring_y = springs[i][1];

        // Animate spring with pulsing effect
        int pulse = (int)(sin(frame * 0.2 + i) * 3);

        for (int dy = -2 - pulse; dy <= 2 + pulse; dy++) {
            for (int dx = -2 - pulse; dx <= 2 + pulse; dx++) {
                int px = spring_x + dx;
                int py = spring_y + dy;
                if (px >= 0 && px < renderer->width && py >= 0 && py < renderer->height) {
                    // Spring color (cyan)
                    terminal_render_set_pixel(renderer, px, py, 0, 255, 255);
                }
            }
        }
    }

    // Draw water flowing from springs
    for (int i = 0; i < 3; i++) {
        int spring_x = springs[i][0];
        int spring_y = springs[i][1];

        // Water drops falling
        for (int drop = 0; drop < 3; drop++) {
            int water_x = spring_x + (int)(sin(frame * 0.1 + i + drop) * 5);
            int water_y = spring_y + 10 + drop * 5 + (frame / 10) % 20;

            if (water_y < renderer->height) {
                terminal_render_set_pixel(renderer, water_x, water_y, 0, 191, 255);
            }
        }
    }
}

int main() {
    printf("=== VERSE Terminal Render Test ===\n");
    printf("Testing Unicode-based video streaming to terminal\n\n");

    // Check terminal support
    if (!terminal_render_supports_color()) {
        printf("Warning: Terminal may not support color\n");
    }

    int term_width, term_height;
    terminal_render_get_dimensions(&term_width, &term_height);
    printf("Terminal dimensions: %dx%d\n", term_width, term_height);

    // Create renderer for our game resolution (256x240)
    TerminalRenderer* renderer = terminal_render_create(256, 240);
    if (!renderer) {
        printf("Failed to create terminal renderer\n");
        return 1;
    }

    printf("Created renderer: %dx%d\n", renderer->width, renderer->height);
    printf("Press Ctrl+C to exit\n\n");

    // Animation loop
    int frame = 0;
    while (1) {
        // Cycle through different demos
        int demo = (frame / 100) % 3;

        switch (demo) {
            case 0:
                draw_test_pattern(renderer, frame);
                break;
            case 1:
                draw_game_simulation(renderer, frame);
                break;
            case 2:
                draw_spring_world_demo(renderer, frame);
                break;
        }

        // Present to terminal
        terminal_render_present(renderer);

        // Frame rate control
        usleep(50000); // 20 FPS
        frame++;
    }

    terminal_render_destroy(renderer);
    return 0;
}
