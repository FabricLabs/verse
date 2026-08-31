/*
 * test_webgpu_renderer.cpp - Test program for WebGPU first-person renderer
 *
 * This demonstrates the modern WebGPU-based rendering system with
 * support for the new VOXEL_PLASTIC and VOXEL_CLOTH materials.
 */

#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
#include <dawn/webgpu_cpp.h>
#include <glm/glm.hpp>

#include "webgpu_fp_renderer.h"
#include "world.h"
#include "world_generation.h"
#include "world_core.h"
#include "voxel.h"

// Platform-specific surface creation
#ifdef _WIN32
    #include <windows.h>
#elif defined(__APPLE__)
    #include <Cocoa/Cocoa.h>
#else
    #include <X11/Xlib.h>
#endif

class WebGPURendererTest {
public:
    WebGPURendererTest() : window(nullptr), world(nullptr), running(true) {
        mouseCapture = false;
        moveSpeed = 10.0f;
        mouseSensitivity = 0.002f;
    }

    ~WebGPURendererTest() {
        cleanup();
    }

    bool initialize() {
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
            return false;
        }

        // Create window
        window = SDL_CreateWindow("WebGPU First-Person Renderer Test",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 1280, 720,
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            return false;
        }

        // Initialize Dawn/WebGPU
        if (!initializeWebGPU()) {
            return false;
        }

        // Create test world
        createTestWorld();

        // Initialize renderer
        renderer = std::make_unique<verse::WebGPUFPRenderer>();
        if (!renderer->initialize(device, surface, 1280, 720)) {
            std::cerr << "Failed to initialize WebGPU renderer" << std::endl;
            return false;
        }

        renderer->setWorld(world);

        // Set initial camera position
        renderer->getCamera().setPosition(glm::vec3(16, 16, 20));
        renderer->getCamera().setRotation(0, 0);

        return true;
    }

    void run() {
        Uint32 lastTime = SDL_GetTicks();

        while (running) {
            // Handle events
            handleEvents();

            // Calculate delta time
            Uint32 currentTime = SDL_GetTicks();
            float deltaTime = (currentTime - lastTime) / 1000.0f;
            lastTime = currentTime;

            // Update
            update(deltaTime);

            // Render
            renderer->render();

            // Cap frame rate
            SDL_Delay(16); // ~60 FPS
        }
    }

private:
    SDL_Window* window;
    World* world;
    bool running;
    bool mouseCapture;
    float moveSpeed;
    float mouseSensitivity;

    // WebGPU resources
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Surface surface;

    std::unique_ptr<verse::WebGPUFPRenderer> renderer;

    bool initializeWebGPU() {
        // Create Dawn instance
        instance = wgpu::CreateInstance();
        if (!instance) {
            std::cerr << "Failed to create WebGPU instance" << std::endl;
            return false;
        }

        // Request adapter
        wgpu::RequestAdapterOptions adapterOptions{};
        adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;

        adapter = instance.RequestAdapter(&adapterOptions);
        if (!adapter) {
            std::cerr << "Failed to get WebGPU adapter" << std::endl;
            return false;
        }

        // Request device
        wgpu::DeviceDescriptor deviceDesc{};
        device = adapter.RequestDevice(&deviceDesc);
        if (!device) {
            std::cerr << "Failed to create WebGPU device" << std::endl;
            return false;
        }

        // Set error callback
        device.SetUncapturedErrorCallback([](wgpu::ErrorType type, const char* message) {
            std::cerr << "WebGPU error: " << message << std::endl;
        });

        // Create surface
        surface = createSurfaceForWindow();
        if (!surface) {
            std::cerr << "Failed to create WebGPU surface" << std::endl;
            return false;
        }

        return true;
    }

    wgpu::Surface createSurfaceForWindow() {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
            return nullptr;
        }

        wgpu::SurfaceDescriptor surfaceDesc{};

#ifdef _WIN32
        wgpu::SurfaceDescriptorFromWindowsHWND hwndDesc{};
        hwndDesc.hwnd = wmInfo.info.win.window;
        hwndDesc.hinstance = GetModuleHandle(nullptr);
        surfaceDesc.nextInChain = &hwndDesc;
#elif defined(__APPLE__)
        // Create Metal layer
        NSWindow* nsWindow = wmInfo.info.cocoa.window;
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        [nsWindow.contentView setLayer:metalLayer];
        [nsWindow.contentView setWantsLayer:YES];

        wgpu::SurfaceDescriptorFromMetalLayer metalDesc{};
        metalDesc.layer = metalLayer;
        surfaceDesc.nextInChain = &metalDesc;
#else
        wgpu::SurfaceDescriptorFromXlibWindow xlibDesc{};
        xlibDesc.display = wmInfo.info.x11.display;
        xlibDesc.window = wmInfo.info.x11.window;
        surfaceDesc.nextInChain = &xlibDesc;
#endif

        return instance.CreateSurface(&surfaceDesc);
    }

    void createTestWorld() {
        // Create a 32x32x32 world
        world = world_create(32, 32, 32);

        // Generate a simple test world with various materials
        for (int z = 0; z < 16; z++) {
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    if (z == 0) {
                        // Bedrock floor
                        world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
                    } else if (z < 5) {
                        // Stone layers
                        world_set_voxel(world, x, y, z, VOXEL_STONE);
                    } else if (z == 5) {
                        // Surface with various materials
                        if (x < 8 && y < 8) {
                            world_set_voxel(world, x, y, z, VOXEL_GRASS);
                        } else if (x >= 8 && x < 16 && y < 8) {
                            world_set_voxel(world, x, y, z, VOXEL_PLASTIC);
                        } else if (x >= 16 && x < 24 && y < 8) {
                            world_set_voxel(world, x, y, z, VOXEL_CLOTH);
                        } else if (x >= 24 && y < 8) {
                            world_set_voxel(world, x, y, z, VOXEL_SAND);
                        } else if (y >= 8 && y < 16) {
                            // Metal row
                            VoxelType metals[] = {VOXEL_IRON, VOXEL_COPPER, VOXEL_SILVER, VOXEL_GOLD};
                            world_set_voxel(world, x, y, z, metals[(x / 8) % 4]);
                        } else if (y >= 16 && y < 24) {
                            // Crystal row
                            VoxelType crystals[] = {VOXEL_CRYSTAL, VOXEL_CRYSTAL_RED,
                                                   VOXEL_CRYSTAL_GREEN, VOXEL_CRYSTAL_BLUE};
                            world_set_voxel(world, x, y, z, crystals[(x / 8) % 4]);
                        } else {
                            world_set_voxel(world, x, y, z, VOXEL_STONE);
                        }
                    }
                }
            }
        }

        // Add some structures
        // Glass cube
        for (int z = 6; z < 10; z++) {
            for (int y = 4; y < 8; y++) {
                for (int x = 4; x < 8; x++) {
                    if (z == 6 || z == 9 || y == 4 || y == 7 || x == 4 || x == 7) {
                        world_set_voxel(world, x, y, z, VOXEL_GLASS);
                    }
                }
            }
        }

        // Water pool
        for (int y = 12; y < 20; y++) {
            for (int x = 12; x < 20; x++) {
                world_set_voxel(world, x, y, 5, VOXEL_CLAY);
                world_set_voxel(world, x, y, 6, VOXEL_WATER);
            }
        }

        // Magma pit
        for (int y = 24; y < 28; y++) {
            for (int x = 24; x < 28; x++) {
                world_set_voxel(world, x, y, 4, VOXEL_OBSIDIAN);
                world_set_voxel(world, x, y, 5, VOXEL_MAGMA);
            }
        }
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN:
                    handleKeyPress(event.key.keysym.sym);
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouseCapture = !mouseCapture;
                        SDL_SetRelativeMouseMode(mouseCapture ? SDL_TRUE : SDL_FALSE);
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (mouseCapture) {
                        renderer->getCamera().rotate(
                            event.motion.xrel * mouseSensitivity,
                            -event.motion.yrel * mouseSensitivity
                        );
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        renderer->resize(event.window.data1, event.window.data2);
                    }
                    break;
            }
        }
    }

    void handleKeyPress(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE:
                running = false;
                break;
            case SDLK_F1:
                renderer->enableShadows(!renderer->shadowsEnabled);
                break;
            case SDLK_F2:
                renderer->enableSSAO(!renderer->ssaoEnabled);
                break;
            case SDLK_r:
                renderer->invalidateMesh();
                break;
        }
    }

    void update(float deltaTime) {
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);

        float moveDistance = moveSpeed * deltaTime;

        if (keyState[SDL_SCANCODE_W]) {
            renderer->getCamera().moveForward(moveDistance);
        }
        if (keyState[SDL_SCANCODE_S]) {
            renderer->getCamera().moveForward(-moveDistance);
        }
        if (keyState[SDL_SCANCODE_A]) {
            renderer->getCamera().moveRight(-moveDistance);
        }
        if (keyState[SDL_SCANCODE_D]) {
            renderer->getCamera().moveRight(moveDistance);
        }
        if (keyState[SDL_SCANCODE_SPACE]) {
            renderer->getCamera().moveUp(moveDistance);
        }
        if (keyState[SDL_SCANCODE_LSHIFT]) {
            renderer->getCamera().moveUp(-moveDistance);
        }
    }

    void cleanup() {
        renderer.reset();

        if (world) {
            world_destroy(world);
            world = nullptr;
        }

        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "=== WebGPU First-Person Renderer Test ===" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move" << std::endl;
    std::cout << "  Space/Shift - Up/Down" << std::endl;
    std::cout << "  Left Click - Toggle mouse capture" << std::endl;
    std::cout << "  Mouse - Look around (when captured)" << std::endl;
    std::cout << "  F1 - Toggle shadows" << std::endl;
    std::cout << "  F2 - Toggle SSAO" << std::endl;
    std::cout << "  R - Rebuild mesh" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    std::cout << std::endl;

    WebGPURendererTest test;
    if (test.initialize()) {
        test.run();
    }

    return 0;
}
