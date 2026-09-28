#pragma once
#include <stddef.h>
#include <stdint.h>

/* N3S1 values are decoded explicitly as little endian; no on-disk C structs. */
#define SCENE_MAX_BYTES (4u*1024u*1024u)
#define SCENE_TEXTURE_BUDGET (5u*1024u*1024u)
typedef struct { float pos[3], uv[2], color[4]; } SceneVertex;
typedef struct { uint32_t first, count, material; float center[3]; } SceneBatch;
typedef struct {
    char key[64], texture[112];
    uint32_t alpha_mode, width, height;
} SceneMaterial;
typedef struct {
    uint32_t vertex_count, index_count, batch_count, material_count;
    float bounds[6];
    char section[64], source[88];
    uint8_t normalized_sha256[32];
    SceneVertex *vertices;
    uint16_t *indices;
    SceneBatch *batches;
    SceneMaterial *materials;
    size_t geometry_bytes, texture_bytes;
} SceneSection;
/* Returns a complete owned section or NULL. Never publishes partial state. */
SceneSection *scene_decode(const void *bytes, size_t size);
SceneSection *scene_load(const char *path);
void scene_free(SceneSection *scene);
