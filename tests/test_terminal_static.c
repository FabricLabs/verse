#include "terminal_render.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Testing Terminal Renderer (Static)...\n");

    // Create renderer for 64x48 (scaled down from 256x240 for terminal)
    TerminalRenderer* renderer = terminal_render_create(64, 48);
    if (!renderer) {
        printf("Failed to create terminal renderer\n");
        return 1;
    }

    printf("Terminal renderer created successfully\n");
    printf("Buffer size: %d bytes\n", renderer->width * renderer->height * 4);

    // Draw a simple test pattern
    terminal_render_clear(renderer);

    // Draw a gradient
    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            uint8_t r = (uint8_t)((x * 255) / renderer->width);
            uint8_t g = (uint8_t)((y * 255) / renderer->height);
            uint8_t b = (uint8_t)(((x + y) * 255) / (renderer->width + renderer->height));

            terminal_render_set_pixel(renderer, x, y, r, g, b);
        }
    }

    // Draw a red circle in the center
    int center_x = renderer->width / 2;
    int center_y = renderer->height / 2;
    int radius = 8;

    for (int y = 0; y < renderer->height; y++) {
        for (int x = 0; x < renderer->width; x++) {
            int dx = x - center_x;
            int dy = y - center_y;
            if (dx * dx + dy * dy <= radius * radius) {
                terminal_render_set_pixel(renderer, x, y, 255, 0, 0);
            }
        }
    }

    printf("Pattern drawn, rendering to terminal...\n");
    terminal_render_to_terminal(renderer);

    printf("\nPress Enter to continue...");
    getchar();

    terminal_render_destroy(renderer);
    printf("Terminal renderer destroyed\n");

    return 0;
}
