#include "terminal_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

// Unicode block characters for different brightness levels
const char* UNICODE_BLOCKS[5] = {
    " ",     // 0/4 - Empty
    "▄",     // 1/4 - Lower half
    "▀",     // 2/4 - Upper half
    "█",     // 3/4 - Full block
    "█"      // 4/4 - Full block (same as 3/4)
};

TerminalRenderer* terminal_render_create(int width, int height) {
    TerminalRenderer* renderer = malloc(sizeof(TerminalRenderer));
    if (!renderer) return NULL;

    renderer->width = width;
    renderer->height = height;
    renderer->initialized = false;

    // Allocate buffers
    renderer->buffer = malloc(width * height * 4); // RGBA
    renderer->color_buffer = malloc(width * height);

    if (!renderer->buffer || !renderer->color_buffer) {
        terminal_render_destroy(renderer);
        return NULL;
    }

    // Get terminal dimensions
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        renderer->terminal_width = w.ws_col;
        renderer->terminal_height = w.ws_row;
    } else {
        renderer->terminal_width = 80;
        renderer->terminal_height = 24;
    }

    // Initialize terminal for raw mode
    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    // Clear screen and hide cursor
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);

    renderer->initialized = true;
    return renderer;
}

void terminal_render_destroy(TerminalRenderer* renderer) {
    if (!renderer) return;

    // Restore terminal
    printf("\033[?25h\033[0m");
    fflush(stdout);

    // Restore termios
    struct termios old_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    old_termios.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

    free(renderer->buffer);
    free(renderer->color_buffer);
    free(renderer);
}

void terminal_render_set_pixel(TerminalRenderer* renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!renderer || !renderer->initialized) return;
    if (x < 0 || x >= renderer->width || y < 0 || y >= renderer->height) return;

    int index = (y * renderer->width + x) * 4;
    renderer->buffer[index] = r;
    renderer->buffer[index + 1] = g;
    renderer->buffer[index + 2] = b;
    renderer->buffer[index + 3] = (uint8_t)255; // Alpha
}

void terminal_render_clear(TerminalRenderer* renderer) {
    if (!renderer || !renderer->initialized) return;

    memset(renderer->buffer, 0, renderer->width * renderer->height * 4);
    memset(renderer->color_buffer, 0, renderer->width * renderer->height);
}

int terminal_render_rgb_to_ansi(uint8_t r, uint8_t g, uint8_t b) {
    // Convert RGB to 256-color ANSI
    // Use the standard 6x6x6 color cube + grayscale
    int ri = (r * 5) / 255;
    int gi = (g * 5) / 255;
    int bi = (b * 5) / 255;

    if (ri == gi && gi == bi && ri > 0) {
        // Grayscale
        return 232 + (ri * 23) / 255;
    } else {
        // Color cube
        return 16 + (ri * 36) + (gi * 6) + bi;
    }
}

uint8_t terminal_render_rgb_to_grayscale(uint8_t r, uint8_t g, uint8_t b) {
    return (r * 299 + g * 587 + b * 114) / 1000;
}

void terminal_render_present(TerminalRenderer* renderer) {
    if (!renderer || !renderer->initialized) return;

    // Move cursor to top-left
    printf("\033[H");

    // Calculate scaling factors
    int scale_x = renderer->terminal_width / (renderer->width / 2);
    int scale_y = renderer->terminal_height / (renderer->height / 2);
    int scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1) scale = 1;

    // Render using Unicode blocks (2x2 pixels per character)
    for (int y = 0; y < renderer->height; y += 2) {
        for (int x = 0; x < renderer->width; x += 2) {
            // Get 4 pixels (2x2 block)
            uint8_t pixels[4][3];
            int pixel_count = 0;

            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int px = x + dx;
                    int py = y + dy;

                    if (px < renderer->width && py < renderer->height) {
                        int index = (py * renderer->width + px) * 4;
                        pixels[pixel_count][0] = renderer->buffer[index];
                        pixels[pixel_count][1] = renderer->buffer[index + 1];
                        pixels[pixel_count][2] = renderer->buffer[index + 2];
                        pixel_count++;
                    }
                }
            }

            // Calculate average brightness for this 2x2 block
            int total_brightness = 0;
            for (int i = 0; i < pixel_count; i++) {
                total_brightness += terminal_render_rgb_to_grayscale(
                    pixels[i][0], pixels[i][1], pixels[i][2]);
            }
            int avg_brightness = total_brightness / pixel_count;

            // Choose Unicode block based on brightness
            int block_index = (avg_brightness * 4) / 256;
            if (block_index > 4) block_index = 4;

            // Calculate average color
            int total_r = 0, total_g = 0, total_b = 0;
            for (int i = 0; i < pixel_count; i++) {
                total_r += pixels[i][0];
                total_g += pixels[i][1];
                total_b += pixels[i][2];
            }
            int avg_r = total_r / pixel_count;
            int avg_g = total_g / pixel_count;
            int avg_b = total_b / pixel_count;

            // Set color and print block
            int color_code = terminal_render_rgb_to_ansi(avg_r, avg_g, avg_b);
            printf("\033[38;5;%dm%s", color_code, UNICODE_BLOCKS[block_index]);
        }
        printf("\n");
    }

    fflush(stdout);
}

void terminal_render_get_dimensions(int* width, int* height) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        *width = w.ws_col;
        *height = w.ws_row;
    } else {
        *width = 80;
        *height = 24;
    }
}

bool terminal_render_supports_color(void) {
    const char* term = getenv("TERM");
    return term && (strstr(term, "xterm") || strstr(term, "color"));
}

void terminal_render_to_terminal(TerminalRenderer* renderer) {
    if (!renderer || !renderer->initialized) return;

    // Clear screen and move cursor to top-left
    printf("\033[2J\033[H");

    // Calculate scaling factor to fit in terminal
    int scale_x = renderer->width / renderer->terminal_width;
    int scale_y = renderer->height / renderer->terminal_height;
    int scale = (scale_x > scale_y) ? scale_x : scale_y;
    if (scale < 1) scale = 1;

    // Render using Unicode block characters
    for (int y = 0; y < renderer->height; y += scale * 2) {
        for (int x = 0; x < renderer->width; x += scale) {
            // Get colors for upper and lower halves
            int upper_x = x;
            int upper_y = y;
            int lower_x = x;
            int lower_y = y + scale;

            // Get colors (clamp to bounds)
            if (upper_x >= renderer->width) upper_x = renderer->width - 1;
            if (upper_y >= renderer->height) upper_y = renderer->height - 1;
            if (lower_x >= renderer->width) lower_x = renderer->width - 1;
            if (lower_y >= renderer->height) lower_y = renderer->height - 1;

            int upper_idx = (upper_y * renderer->width + upper_x) * 4;
            int lower_idx = (lower_y * renderer->width + lower_x) * 4;

            uint8_t upper_r = renderer->buffer[upper_idx];
            uint8_t upper_g = renderer->buffer[upper_idx + 1];
            uint8_t upper_b = renderer->buffer[upper_idx + 2];
            uint8_t lower_r = renderer->buffer[lower_idx];
            uint8_t lower_g = renderer->buffer[lower_idx + 1];
            uint8_t lower_b = renderer->buffer[lower_idx + 2];

            // Calculate brightness for upper and lower
            int upper_brightness = (upper_r + upper_g + upper_b) / 3;
            int lower_brightness = (lower_r + lower_g + lower_b) / 3;

            // Choose Unicode block character based on brightness
            int block_idx = 0;
            if (upper_brightness > 192 && lower_brightness > 192) block_idx = 4; // █
            else if (upper_brightness > 192 && lower_brightness > 64) block_idx = 2; // ▀
            else if (upper_brightness > 64 && lower_brightness > 192) block_idx = 1; // ▄
            else if (upper_brightness > 64 || lower_brightness > 64) block_idx = 3; // █
            else block_idx = 0; // Space

            // Set foreground color (use upper color)
            int color = terminal_render_rgb_to_ansi(upper_r, upper_g, upper_b);
            printf("\033[38;5;%dm", color);

            // Print Unicode block
            printf("%s", UNICODE_BLOCKS[block_idx]);
        }
        printf("\n");
    }

    // Reset color
    printf("\033[0m");
    fflush(stdout);
}
