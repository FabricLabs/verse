#include "webgpu_fp_renderer.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace verse {

// Shader source code
static const char* kVertexShader = R"(
struct CameraUniforms {
    viewProjection : mat4x4<f32>,
    cameraPos : vec3<f32>,
    time : f32,
};

struct VoxelVertex {
    @location(0) position : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) voxelType : u32,
    @location(4) ambient : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) voxelType : u32,
    @location(4) ambient : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;

@vertex
fn main(input : VoxelVertex) -> VertexOutput {
    var output : VertexOutput;

    output.position = camera.viewProjection * vec4<f32>(input.position, 1.0);
    output.worldPos = input.position;
    output.normal = input.normal;
    output.texCoord = input.texCoord;
    output.voxelType = input.voxelType;
    output.ambient = f32(input.ambient) / 255.0;

    return output;
}
)";

static const char* kFragmentShader = R"(
struct CameraUniforms {
    viewProjection : mat4x4<f32>,
    cameraPos : vec3<f32>,
    time : f32,
};

struct LightingUniforms {
    sunDirection : vec3<f32>,
    sunIntensity : f32,
    sunColor : vec3<f32>,
    ambientIntensity : f32,
    fogColor : vec3<f32>,
    fogStart : f32,
    fogEnd : f32,
};

struct VoxelMaterial {
    albedo : vec4<f32>,
    properties : vec4<f32>, // roughness, metallic, emission, reserved
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(1) @binding(0) var<uniform> lighting : LightingUniforms;
@group(2) @binding(0) var<storage, read> materials : array<VoxelMaterial>;

struct FragmentInput {
    @location(0) worldPos : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) voxelType : u32,
    @location(4) ambient : f32,
};

@fragment
fn main(input : FragmentInput) -> @location(0) vec4<f32> {
    let material = materials[input.voxelType];
    let albedo = material.albedo.rgb;
    let roughness = material.properties.x;
    let metallic = material.properties.y;
    let emission = material.properties.z;

    // Basic lighting calculation
    let N = normalize(input.normal);
    let L = normalize(-lighting.sunDirection);
    let V = normalize(camera.cameraPos - input.worldPos);
    let H = normalize(L + V);

    // Diffuse lighting
    let NdotL = max(dot(N, L), 0.0);
    let diffuse = albedo * lighting.sunColor * lighting.sunIntensity * NdotL;

    // Specular lighting (simplified Blinn-Phong)
    let NdotH = max(dot(N, H), 0.0);
    let specularPower = mix(16.0, 256.0, 1.0 - roughness);
    let specular = lighting.sunColor * pow(NdotH, specularPower) * (1.0 - roughness) * lighting.sunIntensity;

    // Ambient lighting with AO
    let ambient = albedo * lighting.ambientIntensity * input.ambient;

    // Combine lighting
    var finalColor = diffuse + specular + ambient + albedo * emission;

    // Apply fog
    let distance = length(camera.cameraPos - input.worldPos);
    let fogFactor = clamp((lighting.fogEnd - distance) / (lighting.fogEnd - lighting.fogStart), 0.0, 1.0);
    finalColor = mix(lighting.fogColor, finalColor, fogFactor);

    return vec4<f32>(finalColor, material.albedo.a);
}
)";

// FPCamera implementation
FPCamera::FPCamera()
    : position(0.0f, 0.0f, 5.0f)
    , forward(0.0f, 0.0f, -1.0f)
    , right(1.0f, 0.0f, 0.0f)
    , up(0.0f, 1.0f, 0.0f)
    , yaw(0.0f)
    , pitch(0.0f)
    , fov(glm::radians(60.0f))
    , aspectRatio(16.0f / 9.0f) {
}

void FPCamera::setRotation(float newYaw, float newPitch) {
    yaw = newYaw;
    pitch = glm::clamp(newPitch, -glm::radians(89.0f), glm::radians(89.0f));
    updateVectors();
}

void FPCamera::updateVectors() {
    // Calculate forward vector
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = -cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

    // Calculate right and up vectors
    right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    up = glm::normalize(glm::cross(right, forward));
}

glm::mat4 FPCamera::getViewMatrix() const {
    return glm::lookAt(position, position + forward, up);
}

glm::mat4 FPCamera::getProjectionMatrix() const {
    return glm::perspective(fov, aspectRatio, 0.1f, 1000.0f);
}

glm::mat4 FPCamera::getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
}

void FPCamera::moveForward(float delta) {
    position += forward * delta;
}

void FPCamera::moveRight(float delta) {
    position += right * delta;
}

void FPCamera::moveUp(float delta) {
    position += glm::vec3(0.0f, 1.0f, 0.0f) * delta;
}

void FPCamera::rotate(float deltaYaw, float deltaPitch) {
    setRotation(yaw + deltaYaw, pitch + deltaPitch);
}

// WebGPUFPRenderer implementation
WebGPUFPRenderer::WebGPUFPRenderer()
    : meshDirty(true)
    , currentWorld(nullptr)
    , width(1280)
    , height(720)
    , renderDistance(100.0f)
    , shadowsEnabled(false)
    , ssaoEnabled(false)
    , msaaSamples(1) {

    // Initialize default voxel materials
    voxelMaterials.resize(VOXEL_COUNT);
    updateVoxelMaterials();

    // Default lighting
    lightingUniforms.sunDirection = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    lightingUniforms.sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
    lightingUniforms.sunIntensity = 1.0f;
    lightingUniforms.ambientIntensity = 0.3f;
    lightingUniforms.fogColor = glm::vec3(0.7f, 0.8f, 0.9f);
    lightingUniforms.fogStart = 50.0f;
    lightingUniforms.fogEnd = 200.0f;
}

WebGPUFPRenderer::~WebGPUFPRenderer() {
    shutdown();
}

bool WebGPUFPRenderer::initialize(wgpu::Device dev, wgpu::Surface surf,
                                  uint32_t w, uint32_t h) {
    device = dev;
    surface = surf;
    queue = device.GetQueue();
    width = w;
    height = h;

    camera.setAspectRatio((float)width / (float)height);

    try {
        createSwapChain();
        createPipelines();
        createBuffers();
        createTextures();
        createBindGroups();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize WebGPUFPRenderer: " << e.what() << std::endl;
        return false;
    }
}

void WebGPUFPRenderer::shutdown() {
    // WebGPU resources are automatically released by their destructors
    vertices.clear();
    indices.clear();
}

void WebGPUFPRenderer::setWorld(const World* world) {
    if (currentWorld != world) {
        currentWorld = world;
        meshDirty = true;
    }
}

void WebGPUFPRenderer::invalidateMesh() {
    meshDirty = true;
}

void WebGPUFPRenderer::rebuildMesh() {
    if (currentWorld && meshDirty) {
        buildMeshFromWorld();
        uploadMeshData();
        meshDirty = false;
    }
}

void WebGPUFPRenderer::render() {
    if (!currentWorld) return;

    // Rebuild mesh if needed
    if (meshDirty) {
        rebuildMesh();
    }

    // Update uniforms
    updateUniforms();

    // Get next texture from swap chain
    wgpu::TextureView nextTexture = swapChain.GetCurrentTextureView();
    if (!nextTexture) {
        std::cerr << "Failed to get swap chain texture" << std::endl;
        return;
    }

    // Create command encoder
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    // Main render pass
    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view = nextTexture;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = {lightingUniforms.fogColor.r,
                                  lightingUniforms.fogColor.g,
                                  lightingUniforms.fogColor.b, 1.0f};

    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthTextureView;
    depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    depthAttachment.depthClearValue = 1.0f;

    wgpu::RenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &colorAttachment;
    passDescriptor.depthStencilAttachment = &depthAttachment;

    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDescriptor);

    // Render the mesh
    if (!indices.empty()) {
        pass.SetPipeline(mainPipeline);
        pass.SetBindGroup(0, cameraBindGroup);
        pass.SetBindGroup(1, lightingBindGroup);
        pass.SetBindGroup(2, materialBindGroup);
        pass.SetVertexBuffer(0, vertexBuffer);
        pass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);
        pass.DrawIndexed(indices.size());
    }

    pass.End();

    // Submit commands
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
    swapChain.Present();
}

void WebGPUFPRenderer::resize(uint32_t w, uint32_t h) {
    if (width != w || height != h) {
        width = w;
        height = h;
        camera.setAspectRatio((float)width / (float)height);
        createSwapChain();
        createTextures();
    }
}

void WebGPUFPRenderer::updateVoxelMaterials() {
    // Set default materials based on voxel types
    for (int i = 0; i < VOXEL_COUNT; i++) {
        VoxelType type = (VoxelType)i;
        VoxelMaterial& mat = voxelMaterials[i];

        // Get color from voxel type
        uint8_t r, g, b;
        world_voxel_type_color(type, &r, &g, &b);
        mat.albedo = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);

        // Set material properties based on type
        switch (type) {
            case VOXEL_WATER:
                mat.properties = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Low roughness
                mat.albedo.a = 0.8f; // Transparent
                break;
            case VOXEL_GLASS:
                mat.properties = glm::vec4(0.1f, 0.0f, 0.0f, 0.0f);
                mat.albedo.a = 0.3f;
                break;
            case VOXEL_PLASTIC:
                mat.properties = glm::vec4(0.2f, 0.0f, 0.0f, 0.0f); // Smooth plastic
                break;
            case VOXEL_CLOTH:
                mat.properties = glm::vec4(0.9f, 0.0f, 0.0f, 0.0f); // Very rough
                break;
            case VOXEL_GOLD:
            case VOXEL_SILVER:
            case VOXEL_COPPER:
            case VOXEL_IRON:
                mat.properties = glm::vec4(0.3f, 0.9f, 0.0f, 0.0f); // Metallic
                break;
            case VOXEL_MAGMA:
                mat.properties = glm::vec4(0.8f, 0.0f, 3.0f, 0.0f); // Emissive
                break;
            case VOXEL_CRYSTAL:
            case VOXEL_CRYSTAL_RED:
            case VOXEL_CRYSTAL_GREEN:
            case VOXEL_CRYSTAL_BLUE:
                mat.properties = glm::vec4(0.1f, 0.0f, 0.5f, 0.0f); // Slight emission
                mat.albedo.a = 0.7f;
                break;
            default:
                mat.properties = glm::vec4(0.7f, 0.0f, 0.0f, 0.0f); // Default roughness
                break;
        }
    }
}

void WebGPUFPRenderer::createSwapChain() {
    wgpu::SwapChainDescriptor descriptor{};
    descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    descriptor.format = wgpu::TextureFormat::BGRA8Unorm;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.presentMode = wgpu::PresentMode::Fifo;

    swapChain = device.CreateSwapChain(surface, &descriptor);
}

void WebGPUFPRenderer::createPipelines() {
    // Load shaders
    wgpu::ShaderModule vertexModule = loadShaderModule(kVertexShader);
    wgpu::ShaderModule fragmentModule = loadShaderModule(kFragmentShader);

    // Vertex attributes
    std::vector<wgpu::VertexAttribute> attributes(5);
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(VoxelVertex, position);
    attributes[0].shaderLocation = 0;

    attributes[1].format = wgpu::VertexFormat::Float32x3;
    attributes[1].offset = offsetof(VoxelVertex, normal);
    attributes[1].shaderLocation = 1;

    attributes[2].format = wgpu::VertexFormat::Float32x2;
    attributes[2].offset = offsetof(VoxelVertex, texCoord);
    attributes[2].shaderLocation = 2;

    attributes[3].format = wgpu::VertexFormat::Uint32;
    attributes[3].offset = offsetof(VoxelVertex, voxelType);
    attributes[3].shaderLocation = 3;

    attributes[4].format = wgpu::VertexFormat::Uint32;
    attributes[4].offset = offsetof(VoxelVertex, ambient);
    attributes[4].shaderLocation = 4;

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(VoxelVertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = attributes.size();
    vertexBufferLayout.attributes = attributes.data();

    // Pipeline layout
    std::vector<wgpu::BindGroupLayout> bindGroupLayouts;
    // TODO: Create bind group layouts

    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = bindGroupLayouts.size();
    layoutDescriptor.bindGroupLayouts = bindGroupLayouts.data();
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&layoutDescriptor);

    // Create render pipeline
    wgpu::RenderPipelineDescriptor pipelineDescriptor{};
    pipelineDescriptor.layout = pipelineLayout;

    // Vertex stage
    pipelineDescriptor.vertex.module = vertexModule;
    pipelineDescriptor.vertex.entryPoint = "main";
    pipelineDescriptor.vertex.bufferCount = 1;
    pipelineDescriptor.vertex.buffers = &vertexBufferLayout;

    // Fragment stage
    wgpu::FragmentState fragmentState{};
    fragmentState.module = fragmentModule;
    fragmentState.entryPoint = "main";

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = wgpu::TextureFormat::BGRA8Unorm;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::BlendState blendState{};
    blendState.color.operation = wgpu::BlendOperation::Add;
    blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blendState.alpha.operation = wgpu::BlendOperation::Add;
    blendState.alpha.srcFactor = wgpu::BlendFactor::One;
    blendState.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    colorTarget.blend = &blendState;

    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDescriptor.fragment = &fragmentState;

    // Depth stencil state
    wgpu::DepthStencilState depthStencil{};
    depthStencil.depthWriteEnabled = true;
    depthStencil.depthCompare = wgpu::CompareFunction::Less;
    depthStencil.format = wgpu::TextureFormat::Depth24Plus;
    pipelineDescriptor.depthStencil = &depthStencil;

    // Primitive state
    pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDescriptor.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDescriptor.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::Back;

    // Multisample state
    pipelineDescriptor.multisample.count = msaaSamples;
    pipelineDescriptor.multisample.mask = ~0u;
    pipelineDescriptor.multisample.alphaToCoverageEnabled = false;

    mainPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
}

void WebGPUFPRenderer::buildMeshFromWorld() {
    if (!currentWorld) return;

    vertices.clear();
    indices.clear();

    // Use mesh builder to create optimized mesh
    MeshBuilder::buildCulledMesh(currentWorld, camera.getPosition(),
                                 renderDistance, vertices, indices);
}

void WebGPUFPRenderer::updateUniforms() {
    // Update camera uniforms
    cameraUniforms.viewProjection = camera.getViewProjectionMatrix();
    cameraUniforms.cameraPos = camera.getPosition();
    cameraUniforms.time = 0.0f; // TODO: Add time tracking

    queue.WriteBuffer(cameraUniformBuffer, 0, &cameraUniforms, sizeof(cameraUniforms));
    queue.WriteBuffer(lightingUniformBuffer, 0, &lightingUniforms, sizeof(lightingUniforms));
    queue.WriteBuffer(materialBuffer, 0, voxelMaterials.data(),
                     voxelMaterials.size() * sizeof(VoxelMaterial));
}

wgpu::ShaderModule WebGPUFPRenderer::loadShaderModule(const char* source) {
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = source;

    wgpu::ShaderModuleDescriptor descriptor{};
    descriptor.nextInChain = &wgslDesc;

    return device.CreateShaderModule(&descriptor);
}

// MeshBuilder implementation
void MeshBuilder::buildGreedyMesh(const World* world,
                                 std::vector<VoxelVertex>& vertices,
                                 std::vector<uint32_t>& indices) {
    if (!world || !world->voxels) return;

    // Build a greedy mesh similar to voxel_mesh_build_all_faces_greedy
    // This is a simplified version - you'd want to implement the full algorithm

    const int w = world->width;
    const int h = world->height;
    const int d = world->depth;

    // For each face direction
    for (int face = 0; face < 6; face++) {
        // TODO: Implement greedy meshing for each face
        // This would involve finding maximal rectangles of the same voxel type
        // and creating quads for them
    }
}

void MeshBuilder::buildCulledMesh(const World* world,
                                 const glm::vec3& viewPos,
                                 float renderDistance,
                                 std::vector<VoxelVertex>& vertices,
                                 std::vector<uint32_t>& indices) {
    if (!world || !world->voxels) return;

    vertices.clear();
    indices.clear();

    const int w = world->width;
    const int h = world->height;
    const int d = world->depth;

    // Simple per-voxel face generation with distance culling
    for (int z = 0; z < d; z++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Voxel* voxel = world_get_voxel((World*)world, x, y, z);
                if (!voxel || voxel->type == VOXEL_AIR) continue;

                // Distance culling
                glm::vec3 voxelPos(x + 0.5f, y + 0.5f, z + 0.5f);
                float dist = glm::length(voxelPos - viewPos);
                if (dist > renderDistance) continue;

                // Check each face for visibility
                for (int face = 0; face < 6; face++) {
                    // Check if face is exposed (adjacent to air or boundary)
                    int dx = 0, dy = 0, dz = 0;
                    switch (face) {
                        case 0: dz = 1; break;   // +Z (top)
                        case 1: dz = -1; break;  // -Z (bottom)
                        case 2: dy = 1; break;   // +Y
                        case 3: dx = 1; break;   // +X
                        case 4: dy = -1; break;  // -Y
                        case 5: dx = -1; break;  // -X
                    }

                    int nx = x + dx;
                    int ny = y + dy;
                    int nz = z + dz;

                    bool exposed = false;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h || nz < 0 || nz >= d) {
                        exposed = true;
                    } else {
                        Voxel* neighbor = world_get_voxel((World*)world, nx, ny, nz);
                        exposed = (!neighbor || neighbor->type == VOXEL_AIR);
                    }

                    if (!exposed) continue;

                    // Create face quad
                    uint32_t baseIndex = vertices.size();
                    glm::vec3 normal = getFaceNormal(face);
                    uint32_t ao = calculateAmbientOcclusion(world, x, y, z, face);

                    // Add vertices for this face
                    glm::vec3 faceVerts[4];
                    glm::vec2 faceUVs[4];

                    // Generate vertices based on face
                    switch (face) {
                        case 0: // +Z (top)
                            faceVerts[0] = glm::vec3(x, y, z + 1);
                            faceVerts[1] = glm::vec3(x + 1, y, z + 1);
                            faceVerts[2] = glm::vec3(x + 1, y + 1, z + 1);
                            faceVerts[3] = glm::vec3(x, y + 1, z + 1);
                            break;
                        case 1: // -Z (bottom)
                            faceVerts[0] = glm::vec3(x, y, z);
                            faceVerts[1] = glm::vec3(x, y + 1, z);
                            faceVerts[2] = glm::vec3(x + 1, y + 1, z);
                            faceVerts[3] = glm::vec3(x + 1, y, z);
                            break;
                        // TODO: Add other face cases
                    }

                    // Set UVs
                    faceUVs[0] = glm::vec2(0, 0);
                    faceUVs[1] = glm::vec2(1, 0);
                    faceUVs[2] = glm::vec2(1, 1);
                    faceUVs[3] = glm::vec2(0, 1);

                    // Add vertices
                    for (int i = 0; i < 4; i++) {
                        VoxelVertex vert;
                        vert.position = faceVerts[i];
                        vert.normal = normal;
                        vert.texCoord = faceUVs[i];
                        vert.voxelType = voxel->type;
                        vert.ambient = ao;
                        vertices.push_back(vert);
                    }

                    // Add indices for two triangles
                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 1);
                    indices.push_back(baseIndex + 2);

                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 3);
                }
            }
        }
    }
}

uint32_t MeshBuilder::calculateAmbientOcclusion(const World* world,
                                               int x, int y, int z,
                                               int face) {
    // Simple AO calculation
    // TODO: Implement proper ambient occlusion
    return 255; // No occlusion for now
}

glm::vec3 MeshBuilder::getFaceNormal(int face) {
    switch (face) {
        case 0: return glm::vec3(0, 0, 1);   // +Z
        case 1: return glm::vec3(0, 0, -1);  // -Z
        case 2: return glm::vec3(0, 1, 0);   // +Y
        case 3: return glm::vec3(1, 0, 0);   // +X
        case 4: return glm::vec3(0, -1, 0);  // -Y
        case 5: return glm::vec3(-1, 0, 0);  // -X
        default: return glm::vec3(0, 0, 1);
    }
}

// Buffer and texture creation stubs
void WebGPUFPRenderer::createBuffers() {
    // Create uniform buffers
    wgpu::BufferDescriptor bufferDesc{};

    bufferDesc.size = sizeof(CameraUniforms);
    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    cameraUniformBuffer = device.CreateBuffer(&bufferDesc);

    bufferDesc.size = sizeof(LightingUniforms);
    lightingUniformBuffer = device.CreateBuffer(&bufferDesc);

    bufferDesc.size = sizeof(VoxelMaterial) * VOXEL_COUNT;
    bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    materialBuffer = device.CreateBuffer(&bufferDesc);

    // Vertex and index buffers will be created when mesh is built
}

void WebGPUFPRenderer::createTextures() {
    // Create depth texture
    wgpu::TextureDescriptor textureDesc{};
    textureDesc.size = {width, height, 1};
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = msaaSamples;
    textureDesc.dimension = wgpu::TextureDimension::e2D;
    textureDesc.format = wgpu::TextureFormat::Depth24Plus;
    textureDesc.usage = wgpu::TextureUsage::RenderAttachment;

    depthTexture = device.CreateTexture(&textureDesc);
    depthTextureView = depthTexture.CreateView();
}

void WebGPUFPRenderer::createBindGroups() {
    // TODO: Create bind groups after pipeline is properly set up
}

void WebGPUFPRenderer::uploadMeshData() {
    if (vertices.empty() || indices.empty()) return;

    // Create or recreate vertex buffer
    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.size = vertices.size() * sizeof(VoxelVertex);
    bufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBuffer = device.CreateBuffer(&bufferDesc);

    // Create or recreate index buffer
    bufferDesc.size = indices.size() * sizeof(uint32_t);
    bufferDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    indexBuffer = device.CreateBuffer(&bufferDesc);

    // Upload data
    queue.WriteBuffer(vertexBuffer, 0, vertices.data(), vertices.size() * sizeof(VoxelVertex));
    queue.WriteBuffer(indexBuffer, 0, indices.data(), indices.size() * sizeof(uint32_t));
}

} // namespace verse
