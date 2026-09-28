#pragma once
#include "scene_loader.h"
#include <citro3d.h>
typedef struct {
    SceneSection *section;
    C3D_Tex **textures;
    uint32_t *order;
} SceneAsset;
/* Caller synchronizes GPU before release. Transactional; out must be empty. */
int scene_asset_load(SceneAsset *out, const char *path);
void scene_asset_free(SceneAsset *asset);
