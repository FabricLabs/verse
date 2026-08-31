#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 model;
    vec4 cameraPos;
    vec4 lightPos;
    float time;
} ubo;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragPos = worldPos.xyz;
    fragNormal = mat3(ubo.model) * inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
