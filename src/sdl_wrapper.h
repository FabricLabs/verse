/*
 * sdl_wrapper.h - Wrapper to handle different SDL2 include paths
 *
 * This allows the code to use <SDL2/SDL.h> regardless of how SDL2 is installed
 */

#ifndef SDL_WRAPPER_H
#define SDL_WRAPPER_H

// Try different include paths for SDL2
#ifdef __has_include
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #elif __has_include(<SDL.h>)
    #include <SDL.h>
  #else
    #error "SDL2 not found. Please install SDL2 development libraries."
  #endif
#else
  // Fallback for older compilers
  #include <SDL.h>
#endif

#endif // SDL_WRAPPER_H
