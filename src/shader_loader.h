#ifndef SHADER_LOADER_H
#define SHADER_LOADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Shader types
typedef enum {
    SHADER_TYPE_VERTEX,
    SHADER_TYPE_FRAGMENT,
    SHADER_TYPE_COMPUTE
} ShaderType;

// Shader structure
typedef struct {
    char *source_code;
    size_t source_length;
    ShaderType type;
    char *filename;
} Shader;

// Function prototypes
Shader* shader_load_from_file(const char *filename, ShaderType type);
void shader_destroy(Shader *shader);
bool shader_compile_to_spirv(const Shader *shader, uint32_t **spirv_code, size_t *spirv_size);
char* shader_get_default_vertex(void);
char* shader_get_default_fragment(void);

#endif // SHADER_LOADER_H
