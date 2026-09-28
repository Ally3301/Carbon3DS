#pragma once

#include <stdint.h>

#define N3P_MAX_GROUPS 32

typedef enum N3PRenderer {
    N3P_RENDER_TEXTURE_SHINY = 0,
    N3P_RENDER_WINDOW = 1,
    N3P_RENDER_GOURAUD = 2
} N3PRenderer;

typedef struct N3PVertex {
    float pos[3];
    float uv[2];
    float nrm[3];
} N3PVertex;

typedef struct N3PGroup {
    uint32_t index_start;
    uint32_t index_count;
    uint16_t material;
    uint8_t renderer;
    uint8_t reserved;
} N3PGroup;

typedef struct N3PPart {
    N3PVertex *verts;
    uint16_t *indices;
    /* GPU-friendly expanded triangle stream. One vertex per source index.
       Group index_start/index_count are also valid offsets/counts here. */
    N3PVertex *draw_verts;
    N3PGroup *groups;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t draw_vertex_count;
    uint32_t group_count;
} N3PPart;

N3PPart *n3p_load(const char *path);
void n3p_free(N3PPart *part);
