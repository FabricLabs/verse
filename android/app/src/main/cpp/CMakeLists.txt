cmake_minimum_required(VERSION 3.22.1)
project(verse C)

set(CMAKE_C_STANDARD 11)

# SDL2
set(SDL2_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/SDL)
add_subdirectory(${SDL2_DIR} ${CMAKE_BINARY_DIR}/sdl2)

# SDL_ttf + freetype
set(SDL2_TTF_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/SDL_ttf)
add_subdirectory(${SDL2_TTF_DIR} ${CMAKE_BINARY_DIR}/sdl2_ttf)

# Provide SDL2-style include path for SDL_ttf.h
set(SDL2_TTF_GEN ${CMAKE_BINARY_DIR}/sdl2_ttf_gen/include)
file(MAKE_DIRECTORY ${SDL2_TTF_GEN}/SDL2)
configure_file(${SDL2_TTF_DIR}/SDL_ttf.h ${SDL2_TTF_GEN}/SDL2/SDL_ttf.h COPYONLY)

# Jansson JSON library
set(JANSSON_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/jansson)
add_subdirectory(${JANSSON_DIR} ${CMAKE_BINARY_DIR}/jansson)

add_library(
  verse
  SHARED
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/platform_android.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/verse_client.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/game_state.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/dialogue.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/skill.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/player_controls.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/projectile.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/window.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/world.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/world_bulk_ops.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/mob_ai.c
  # Use Android stub for FP renderer to avoid desktop OpenGL calls
  fp_renderer_stub.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/voxel_mesh.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/octree.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/character.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/actor.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/world_transition.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/world_spawn.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/input_manager.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/engine.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/sequencer/melody_loader.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/background_music.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/synthesizer/synthesizer.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/songwriter/songwriter.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/isometric_renderer.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/settings.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/ui_sounds.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/model_transformer.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/greedy_mesh.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/title_hum.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/wfc.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/gpu_voxel_buffer.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/constants.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/universe.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/entropy_field.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/universe_coords.c
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/world_entropy_generator.c
  sdl_entry.c
  # SHA-256 implementation required by character/world
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/noise-c/src/crypto/sha2/sha256.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles3-lib GLESv3)

target_include_directories(verse PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src
  ${SDL2_DIR}/include
  ${SDL2_TTF_DIR}
  ${SDL2_TTF_GEN}
  ${JANSSON_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../src/noise-c/src/crypto/sha2
)

target_compile_definitions(verse PRIVATE __ANDROID__ UI_BUILD INTERFACE_MODE)

target_link_libraries(
  verse
  SDL2
  SDL2_ttf
  ${log-lib}
  ${android-lib}
  ${egl-lib}
  ${gles3-lib}
  jansson
)

# Rename verse_client.c's zero-arg main() to verse_real_main to avoid SDL_main signature mismatch
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../../../../src/verse_client.c
  PROPERTIES COMPILE_DEFINITIONS "main=verse_real_main;SDL_MAIN_HANDLED=1")


