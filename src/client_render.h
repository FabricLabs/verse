/*
 * client_render.h - Rendering pipeline for the verse client
 *
 * Manages screen rendering and presentation.
 */

#ifndef CLIENT_RENDER_H
#define CLIENT_RENDER_H

// Initialize rendering system
void client_render_init(void);

// Main render function (call each frame)
void render_game(void);

#endif // CLIENT_RENDER_H
