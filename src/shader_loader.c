#include "shader_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Load shader from file
Shader* shader_load_from_file(const char *filename, ShaderType type) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open shader file: %s\n", filename);
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fprintf(stderr, "Invalid shader file size: %s\n", filename);
        fclose(file);
        return NULL;
    }

    // Allocate shader structure
    Shader *shader = malloc(sizeof(Shader));
    if (!shader) {
        fprintf(stderr, "Failed to allocate shader structure\n");
        fclose(file);
        return NULL;
    }

    // Allocate memory for source code
    shader->source_code = malloc(file_size + 1);
    if (!shader->source_code) {
        fprintf(stderr, "Failed to allocate shader source code\n");
        free(shader);
        fclose(file);
        return NULL;
    }

    // Read file content
    size_t bytes_read = fread(shader->source_code, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Failed to read shader file: %s\n", filename);
        free(shader->source_code);
        free(shader);
        return NULL;
    }

    // Null-terminate the string
    shader->source_code[file_size] = '\0';
    shader->source_length = file_size;
    shader->type = type;

    // Copy filename
    shader->filename = strdup(filename);
    if (!shader->filename) {
        fprintf(stderr, "Failed to copy filename\n");
        free(shader->source_code);
        free(shader);
        return NULL;
    }

    printf("Loaded shader: %s (%zu bytes)\n", filename, file_size);
    return shader;
}

// Destroy shader
void shader_destroy(Shader *shader) {
    if (!shader) return;

    if (shader->source_code) {
        free(shader->source_code);
    }
    if (shader->filename) {
        free(shader->filename);
    }
    free(shader);
}

// Compile GLSL to SPIR-V (placeholder - would need actual GLSL compiler)
bool shader_compile_to_spirv(const Shader *shader, uint32_t **spirv_code, size_t *spirv_size) {
    if (!shader || !spirv_code || !spirv_size) return false;

    // TODO: Implement actual GLSL to SPIR-V compilation
    // This would typically use glslangValidator or similar tool
    fprintf(stderr, "SPIR-V compilation not yet implemented for: %s\n", shader->filename);
    return false;
}

// Get default vertex shader source
char* shader_get_default_vertex(void) {
    return strdup(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec3 inPosition;\n"
        "layout(location = 1) in vec3 inNormal;\n"
        "layout(location = 2) in vec4 inColor;\n"
        "\n"
        "layout(location = 0) out vec3 fragPos;\n"
        "layout(location = 1) out vec3 fragNormal;\n"
        "layout(location = 2) out vec4 fragColor;\n"
        "\n"
        "layout(set = 0, binding = 0) uniform UniformBufferObject {\n"
        "    mat4 view;\n"
        "    mat4 proj;\n"
        "    mat4 model;\n"
        "} ubo;\n"
        "\n"
        "void main() {\n"
        "    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);\n"
        "    gl_Position = ubo.proj * ubo.view * worldPos;\n"
        "    fragPos = worldPos.xyz;\n"
        "    fragNormal = mat3(ubo.model) * inNormal;\n"
        "    fragColor = inColor;\n"
        "}\n"
    );
}

// Get default fragment shader source
char* shader_get_default_fragment(void) {
    return strdup(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec3 fragPos;\n"
        "layout(location = 1) in vec3 fragNormal;\n"
        "layout(location = 2) in vec4 fragColor;\n"
        "\n"
        "layout(location = 0) out vec4 outColor;\n"
        "\n"
        "void main() {\n"
        "    vec3 N = normalize(fragNormal);\n"
        "    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));\n"
        "    float diff = max(dot(N, lightDir), 0.0);\n"
        "    vec3 ambient = 0.3 * fragColor.rgb;\n"
        "    vec3 diffuse = diff * fragColor.rgb;\n"
        "    outColor = vec4(ambient + diffuse, fragColor.a);\n"
        "}\n"
    );
}
