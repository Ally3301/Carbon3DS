#pragma once

#include "scene_asset.h"
#include "road_network.h"

#define WORLD_STREAM_MAX_SECTIONS 224
#define WORLD_STREAM_MAX_LOADED 6
#define WORLD_STREAM_MAX_WALLS 512

typedef struct {
    float ax, az, bx, bz;
    float min_y, max_y;
} WorldWallSegment;

typedef struct {
    char id[16];
    char path[80];
    float x, y, z;
    float min_x, min_z, max_x, max_z;
    SceneAsset asset;
    unsigned loaded;
    unsigned wall_count;
    WorldWallSegment *walls;
} WorldStreamSection;

typedef struct {
    WorldStreamSection sections[WORLD_STREAM_MAX_SECTIONS];
    unsigned count;
    unsigned loaded;
    unsigned pending;
    float draw_distance;
    float render_distance;
    unsigned collision_pin;
    unsigned load_cooldown;
    RoadNetwork roads;
} WorldStream;

int world_stream_init(WorldStream *world, const char *manifest_path);
void world_stream_update(WorldStream *world, float x, float z, float forward_x, float forward_z);
void world_stream_free(WorldStream *world);
int world_stream_ground_y(WorldStream *world, float x, float z, float ceiling, float *out_y);
int world_stream_wall_contact(const WorldStream *world, float x, float z, float y,
                              float radius, float *out_x, float *out_z, float *out_depth);
