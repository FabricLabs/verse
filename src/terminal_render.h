#ifndef TERMINAL_RENDER_H
#define TERMINAL_RENDER_H

#include <stdint.h>
#include <stdbool.h>

// Terminal renderer for streaming video output
typedef struct {
    int width;
    int height;
    char* buffer;
    char* color_buffer;
    bool initialized;
    int terminal_width;
    int terminal_height;
} TerminalRenderer;

// Initialize terminal renderer
TerminalRenderer* terminal_render_create(int width, int height);

// Destroy terminal renderer
void terminal_render_destroy(TerminalRenderer* renderer);

// Set pixel in terminal buffer
void terminal_render_set_pixel(TerminalRenderer* renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b);

// Clear terminal buffer
void terminal_render_clear(TerminalRenderer* renderer);

// Render buffer to terminal using Unicode characters
void terminal_render_to_terminal(TerminalRenderer* renderer);

// Get terminal dimensions
void terminal_render_get_dimensions(int* width, int* height);

// Convert RGB to ANSI color code
int terminal_render_rgb_to_ansi(uint8_t r, uint8_t g, uint8_t b);

// Convert RGB to grayscale
uint8_t terminal_render_rgb_to_grayscale(uint8_t r, uint8_t g, uint8_t b);

// Unicode block characters for different brightness levels
extern const char* UNICODE_BLOCKS[5];

#endif // TERMINAL_RENDER_H
