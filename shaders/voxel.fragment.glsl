#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 model;
    vec4 cameraPos;
    vec4 lightPos;
    float time;
} ubo;

void main() {
    // Normalize the normal vector
    vec3 N = normalize(fragNormal);

    // Light direction
    vec3 L = normalize(ubo.lightPos.xyz - fragPos);

    // View direction
    vec3 V = normalize(ubo.cameraPos.xyz - fragPos);

    // Half-way vector for Blinn-Phong
    vec3 H = normalize(L + V);

    // Ambient lighting
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * fragColor.rgb;

    // Diffuse lighting
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * fragColor.rgb;

    // Specular lighting (Blinn-Phong)
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    vec3 specular = spec * vec3(1.0, 1.0, 1.0);

    // Combine lighting components
    vec3 result = ambient + diffuse + specular;

    // Add some fog effect based on distance
    float distance = length(ubo.cameraPos.xyz - fragPos);
    float fogFactor = exp(-distance * 0.01);
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    vec3 fogColor = vec3(0.7, 0.8, 1.0);
    result = mix(fogColor, result, fogFactor);

    outColor = vec4(result, fragColor.a);
}
