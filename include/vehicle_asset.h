#pragma once

#include "n3p_loader.h"
#include "texture_uv.h"

#include <citro3d.h>
#include <stdint.h>

#define VEHICLE_MAX_OPTIONS_PER_SLOT 20
#define VEHICLE_MAX_TEXTURES 16
#define VEHICLE_PATH_MAX 96
#define VEHICLE_WHEEL_STYLE_COUNT 3

typedef enum VehiclePartSlot {
    VEHICLE_SLOT_BODY = 0,
    VEHICLE_SLOT_BASE,
    VEHICLE_SLOT_HOOD,
    VEHICLE_SLOT_SPOILER,
    VEHICLE_SLOT_CREWTAG_SIDES,
    VEHICLE_SLOT_CREWTAG_HOOD,
    VEHICLE_SLOT_COUNT
} VehiclePartSlot;

typedef struct VehiclePartOption {
    int index;
    char path[VEHICLE_PATH_MAX];
} VehiclePartOption;

typedef struct VehicleSlotState {
    VehiclePartOption options[VEHICLE_MAX_OPTIONS_PER_SLOT];
    int option_count;
    int selected;
    N3PPart *loaded;
} VehicleSlotState;

typedef struct VehicleTexture {
    uint16_t material;
    int loaded;
    C3D_Tex texture;
    TextureUV uv;
} VehicleTexture;

typedef struct VehicleAsset {
    char id[32];
    char base_path[64];
    VehicleSlotState slots[VEHICLE_SLOT_COUNT];
    VehicleTexture textures[VEHICLE_MAX_TEXTURES];
    int texture_count;
    /* Original RaceCarRenderInfo PAINT material uses the car-specific
       *_DETAILS texture as a secondary texture input.  Keep the original
       numeric texture material id so we can reuse the already loaded T3X. */
    uint16_t paint_details_material;
    /* PSP texture alpha encodes shine/reflection masks, not coverage. */
    int psp_texture_alpha_is_mask;
    uint16_t psp_alpha_coverage[VEHICLE_MAX_TEXTURES];
    int psp_alpha_coverage_count;
    uint32_t paint_rgba;
    float bounds_min[3];
    float bounds_max[3];
    C3D_Tex wheel_texture;
    TextureUV wheel_uv;
    int wheel_texture_loaded;
    int wheel_style;
    char wheel_texture_name[64];

    /* Optional geometry-derived placement written by derive_vehicle_wheel_fit.py. */
    int wheel_fit_valid;
    float wheel_front_x;
    float wheel_rear_x;
    float wheel_half_track;
    float wheel_radius;
    float wheel_center_y;
    int wheel_axles_valid;
    float wheel_front_radius, wheel_rear_radius;
    float wheel_front_y, wheel_rear_y;
    int light_fit_valid;
    float light_front_x, light_front_y, light_front_z, light_front_h, light_front_w;
    float light_rear_x, light_rear_y, light_rear_z, light_rear_h, light_rear_w;
} VehicleAsset;

void vehicle_asset_init(VehicleAsset *asset);
int vehicle_asset_load(VehicleAsset *asset, const char *vehicle_id);
/* Body-only, texture-free garage neighbour; safe to keep several in RAM. */
int vehicle_asset_load_preview(VehicleAsset *asset, const char *vehicle_id);
void vehicle_asset_free(VehicleAsset *asset);

const N3PPart *vehicle_asset_part(const VehicleAsset *asset, VehiclePartSlot slot);
const C3D_Tex *vehicle_asset_texture(const VehicleAsset *asset, uint16_t material);
const TextureUV *vehicle_asset_texture_uv(const VehicleAsset *asset, uint16_t material);
const C3D_Tex *vehicle_asset_paint_details_texture(const VehicleAsset *asset);
const TextureUV *vehicle_asset_paint_details_uv(const VehicleAsset *asset);
uint16_t vehicle_asset_paint_details_material(const VehicleAsset *asset);
void vehicle_asset_set_paint(VehicleAsset *asset, uint32_t rgba);
uint32_t vehicle_asset_paint(const VehicleAsset *asset);

int vehicle_asset_cycle_bodykit(VehicleAsset *asset, int direction);
int vehicle_asset_cycle_part(VehicleAsset *asset, VehiclePartSlot slot, int direction);
int vehicle_asset_select_value(VehicleAsset *asset, VehiclePartSlot slot, int value);
int vehicle_asset_cycle_wheel(VehicleAsset *asset, int direction);
int vehicle_asset_select_wheel(VehicleAsset *asset, int style);
int vehicle_asset_wheel_style(const VehicleAsset *asset);
const char *vehicle_asset_wheel_name(const VehicleAsset *asset);
const C3D_Tex *vehicle_asset_wheel_texture(const VehicleAsset *asset);
const TextureUV *vehicle_asset_wheel_uv(const VehicleAsset *asset);
int vehicle_asset_psp_texture_uses_alpha(const VehicleAsset *asset, uint16_t material);
void vehicle_asset_wheel_placement(const VehicleAsset *asset,
                                   float *front_x, float *rear_x,
                                   float *half_track, float *radius,
                                   float *center_y);
void vehicle_asset_wheel_axles(const VehicleAsset *asset,
                               float *front_radius, float *rear_radius,
                               float *front_y, float *rear_y);

int vehicle_asset_option_count(const VehicleAsset *asset, VehiclePartSlot slot);
int vehicle_asset_selected_value(const VehicleAsset *asset, VehiclePartSlot slot);
int vehicle_asset_selected_ordinal(const VehicleAsset *asset, VehiclePartSlot slot);
const char *vehicle_asset_selected_path(const VehicleAsset *asset, VehiclePartSlot slot);
void vehicle_asset_part_counts(const VehicleAsset *asset, VehiclePartSlot slot,
                               uint32_t *vertices, uint32_t *indices,
                               uint32_t *groups);
const char *vehicle_asset_slot_name(VehiclePartSlot slot);
