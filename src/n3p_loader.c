#include "n3p_loader.h"
#include "resources.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
#include <3ds.h>
#else
#define linearAlloc malloc
#define linearFree free
#endif

_Static_assert(sizeof(N3PVertex) == 32, "N3PVertex GPU ABI");
_Static_assert(sizeof(N3PGroup) == 12, "N3PGroup runtime ABI");

#pragma pack(push, 1)
typedef struct N3PHeader {
    char magic[4];
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t group_count;
    uint32_t flags;
} N3PHeader;

typedef struct N3PFileGroup {
    uint32_t index_start;
    uint32_t index_count;
    uint16_t material;
    uint8_t renderer;
    uint8_t reserved;
} N3PFileGroup;
#pragma pack(pop)

static int checked_add_mul(size_t *value, size_t count, size_t element)
{
    if (count && element > (SIZE_MAX - *value) / count)
        return -1;
    *value += count * element;
    return 0;
}

N3PPart *n3p_load(const char *path)
{
    size_t size = 0;
    uint8_t *data = resource_load(path, &size);
    if (!data || size < sizeof(N3PHeader)) {
        free(data);
        return NULL;
    }

    N3PHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (memcmp(hdr.magic, "N3P1", 4) != 0
        || hdr.version != 1
        || hdr.flags != 0
        || !hdr.vertex_count || hdr.vertex_count > 65535
        || !hdr.index_count || hdr.index_count > 196608 || hdr.index_count % 3
        || !hdr.group_count || hdr.group_count > N3P_MAX_GROUPS) {
        free(data);
        return NULL;
    }

    size_t need = sizeof(N3PHeader);
    if (checked_add_mul(&need, hdr.vertex_count, sizeof(N3PVertex))
        || checked_add_mul(&need, hdr.index_count, sizeof(uint16_t))
        || checked_add_mul(&need, hdr.group_count, sizeof(N3PFileGroup))
        || need != size) {
        free(data);
        return NULL;
    }

    N3PPart *part = calloc(1, sizeof(*part));
    if (!part) {
        free(data);
        return NULL;
    }

    part->vertex_count = hdr.vertex_count;
    part->index_count = hdr.index_count;
    part->group_count = hdr.group_count;
    part->verts = linearAlloc((size_t)part->vertex_count * sizeof(*part->verts));
    part->indices = linearAlloc((size_t)part->index_count * sizeof(*part->indices));
    part->draw_vertex_count = part->index_count;
    part->draw_verts = linearAlloc((size_t)part->draw_vertex_count * sizeof(*part->draw_verts));
    part->groups = calloc(part->group_count, sizeof(*part->groups));
    if (!part->verts || !part->indices || !part->draw_verts || !part->groups)
        goto invalid;

    const uint8_t *p = data + sizeof(N3PHeader);
    memcpy(part->verts, p, (size_t)part->vertex_count * sizeof(*part->verts));
    p += (size_t)part->vertex_count * sizeof(*part->verts);
    memcpy(part->indices, p, (size_t)part->index_count * sizeof(*part->indices));
    p += (size_t)part->index_count * sizeof(*part->indices);

    for (uint32_t i = 0; i < part->vertex_count; ++i) {
        const N3PVertex *v = &part->verts[i];
        const float values[8] = {
            v->pos[0], v->pos[1], v->pos[2],
            v->uv[0], v->uv[1],
            v->nrm[0], v->nrm[1], v->nrm[2]
        };
        for (unsigned k = 0; k < 8; ++k)
            if (!isfinite(values[k]))
                goto invalid;
    }

    for (uint32_t i = 0; i < part->index_count; ++i) {
        if (part->indices[i] >= part->vertex_count)
            goto invalid;
        part->draw_verts[i] = part->verts[part->indices[i]];
    }

    uint32_t covered = 0;
    for (uint32_t i = 0; i < part->group_count; ++i) {
        N3PFileGroup fg;
        memcpy(&fg, p + (size_t)i * sizeof(fg), sizeof(fg));
        if (fg.index_start != covered
            || !fg.index_count || fg.index_count % 3
            || fg.index_start > part->index_count
            || fg.index_count > part->index_count - fg.index_start
            || fg.renderer > N3P_RENDER_GOURAUD
            || fg.reserved > 7)
            goto invalid;

        part->groups[i].index_start = fg.index_start;
        part->groups[i].index_count = fg.index_count;
        part->groups[i].material = fg.material;
        part->groups[i].renderer = fg.renderer;
        part->groups[i].reserved = fg.reserved;
        covered += fg.index_count;
    }
    if (covered != part->index_count)
        goto invalid;

#ifdef __3DS__
    GSPGPU_FlushDataCache(part->verts, (size_t)part->vertex_count * sizeof(*part->verts));
    GSPGPU_FlushDataCache(part->indices, (size_t)part->index_count * sizeof(*part->indices));
    GSPGPU_FlushDataCache(part->draw_verts,
                          (size_t)part->draw_vertex_count * sizeof(*part->draw_verts));
#endif

    free(data);
    return part;

invalid:
    free(data);
    n3p_free(part);
    return NULL;
}

void n3p_free(N3PPart *part)
{
    if (!part)
        return;
    if (part->verts)
        linearFree(part->verts);
    if (part->indices)
        linearFree(part->indices);
    if (part->draw_verts)
        linearFree(part->draw_verts);
    free(part->groups);
    free(part);
}
