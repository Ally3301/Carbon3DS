#pragma once

#include <stdint.h>

typedef enum VehicleMaterialClass {
    VEHICLE_MAT_PAINT = 0,
    VEHICLE_MAT_TEXTURED,
    VEHICLE_MAT_WINDOW,
    VEHICLE_MAT_CARBON,
    VEHICLE_MAT_UNDERBODY,
    VEHICLE_MAT_GOURAUD,
    VEHICLE_MAT_SPECIAL_DYNAMIC,
    VEHICLE_MAT_UNTEXTURED
} VehicleMaterialClass;

/* Pure classification: no GPU state, safe for host tests. */
VehicleMaterialClass vehicle_material_classify(uint8_t renderer,
                                               uint16_t material,
                                               int has_texture);
typedef enum VehicleUVMode {
    VEHICLE_UV_NONE = 0,
    VEHICLE_UV_CLAMP,
    VEHICLE_UV_REPEAT
} VehicleUVMode;

const char *vehicle_material_class_name(VehicleMaterialClass cls);
VehicleUVMode vehicle_material_uv_mode(VehicleMaterialClass cls);
