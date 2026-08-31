#ifndef WEBGPU_FP_RENDERER_H
#define WEBGPU_FP_RENDERER_H

// Dawn/WebGPU headers - adjust paths as needed for your system
#ifdef __has_include
  #if __has_include(<dawn/webgpu.h>)
    #include <dawn/webgpu.h>
    #include <dawn/webgpu_cpp.h>
    #include <dawn/native/DawnNative.h>
  #elif __has_include(<webgpu/webgpu.h>)
    #include <webgpu/webgpu.h>
    #include <webgpu/webgpu_cpp.h>
  #else
    #error "WebGPU headers not found. Please install Dawn or adjust include paths."
  #endif
#else
  #include <dawn/webgpu.h>
  #include <dawn/webgpu_cpp.h>
  #include <dawn/native/DawnNative.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

#include "world.h"
#include "voxel_mesh.h"
#include "voxel.h"

namespace verse {

// Forward declarations
class WebGPUContext;
class MeshBuilder;

// Vertex structure for voxel rendering
struct VoxelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    uint32_t voxelType;  // For material indexing
    uint32_t ambient;    // Ambient occlusion factor
};

// Uniform buffer structures
struct CameraUniforms {
    glm::mat4 viewProjection;
    glm::vec3 cameraPos;
    float time;
};

struct LightingUniforms {
    glm::vec3 sunDirection;
    float sunIntensity;
    glm::vec3 sunColor;
    float ambientIntensity;
    glm::vec3 fogColor;
    float fogStart;
    float fogEnd;
    float padding;
};

// Material properties for each voxel type
struct VoxelMaterial {
    glm::vec4 albedo;       // RGB + alpha
    glm::vec4 properties;   // roughness, metallic, emission, reserved
};

// First-person camera
class FPCamera {
public:
    FPCamera();

    void setPosition(const glm::vec3& pos) { position = pos; }
    void setRotation(float yaw, float pitch);
    void setFOV(float fovDegrees) { fov = glm::radians(fovDegrees); }
    void setAspectRatio(float ratio) { aspectRatio = ratio; }

    glm::vec3 getPosition() const { return position; }
    glm::vec3 getForward() const { return forward; }
    glm::vec3 getRight() const { return right; }
    glm::vec3 getUp() const { return up; }

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::mat4 getViewProjectionMatrix() const;

    // Movement helpers
    void moveForward(float delta);
    void moveRight(float delta);
    void moveUp(float delta);
    void rotate(float deltaYaw, float deltaPitch);

private:
    glm::vec3 position;
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;

    float yaw;    // Rotation around Y axis
    float pitch;  // Rotation around X axis
    float fov;
    float aspectRatio;

    void updateVectors();
};

// WebGPU First-Person Renderer
class WebGPUFPRenderer {
public:
    WebGPUFPRenderer();
    ~WebGPUFPRenderer();

    // Initialize with WebGPU device and surface
    bool initialize(wgpu::Device device, wgpu::Surface surface,
                   uint32_t width, uint32_t height);
    void shutdown();

    // Set the world to render
    void setWorld(const World* world);

    // Update mesh when world changes
    void invalidateMesh();
    void rebuildMesh();

    // Camera control
    FPCamera& getCamera() { return camera; }
    const FPCamera& getCamera() const { return camera; }

    // Rendering
    void render();
    void resize(uint32_t width, uint32_t height);

    // Material system
    void updateVoxelMaterials();
    void setVoxelMaterial(VoxelType type, const VoxelMaterial& material);

    // Lighting control
    void setSunDirection(const glm::vec3& dir);
    void setSunColor(const glm::vec3& color);
    void setSunIntensity(float intensity);
    void setAmbientIntensity(float intensity);
    void setFog(const glm::vec3& color, float start, float end);

    // Performance options
    void setRenderDistance(float distance) { renderDistance = distance; }
    void enableShadows(bool enable) { shadowsEnabled = enable; }
    void enableSSAO(bool enable) { ssaoEnabled = enable; }
    void setMSAASamples(uint32_t samples);

private:
    // WebGPU resources
    wgpu::Device device;
    wgpu::Surface surface;
    wgpu::SwapChain swapChain;
    wgpu::Queue queue;

    // Pipeline resources
    wgpu::RenderPipeline mainPipeline;
    wgpu::RenderPipeline shadowPipeline;
    wgpu::BindGroup cameraBindGroup;
    wgpu::BindGroup lightingBindGroup;
    wgpu::BindGroup materialBindGroup;

    // Buffers
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    wgpu::Buffer cameraUniformBuffer;
    wgpu::Buffer lightingUniformBuffer;
    wgpu::Buffer materialBuffer;

    // Textures
    wgpu::Texture depthTexture;
    wgpu::TextureView depthTextureView;
    wgpu::Texture shadowMap;
    wgpu::TextureView shadowMapView;
    wgpu::Sampler shadowSampler;

    // Mesh data
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t> indices;
    bool meshDirty;

    // Uniforms
    CameraUniforms cameraUniforms;
    LightingUniforms lightingUniforms;
    std::vector<VoxelMaterial> voxelMaterials;

    // State
    const World* currentWorld;
    FPCamera camera;
    uint32_t width, height;
    float renderDistance;
    bool shadowsEnabled;
    bool ssaoEnabled;
    uint32_t msaaSamples;

    // Internal methods
    void createSwapChain();
    void createPipelines();
    void createBuffers();
    void createTextures();
    void createBindGroups();
    void updateUniforms();
    void buildMeshFromWorld();
    void uploadMeshData();

    // Shader loading
    wgpu::ShaderModule loadShaderModule(const char* source);
};

// Mesh building utilities
class MeshBuilder {
public:
    static void buildGreedyMesh(const World* world,
                               std::vector<VoxelVertex>& vertices,
                               std::vector<uint32_t>& indices);

    static void buildCulledMesh(const World* world,
                               const glm::vec3& viewPos,
                               float renderDistance,
                               std::vector<VoxelVertex>& vertices,
                               std::vector<uint32_t>& indices);

private:
    static uint32_t calculateAmbientOcclusion(const World* world,
                                             int x, int y, int z,
                                             int face);
    static bool isVoxelVisible(const World* world, int x, int y, int z);
    static glm::vec3 getFaceNormal(int face);
};

} // namespace verse

#endif // WEBGPU_FP_RENDERER_H
