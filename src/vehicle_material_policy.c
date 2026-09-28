#include "vehicle_material_policy.h"
#include "n3p_loader.h"

VehicleMaterialClass vehicle_material_classify(uint8_t renderer,
                                               uint16_t material,
                                               int has_texture)
{
    if (renderer == N3P_RENDER_WINDOW || material == 1501)
        return VEHICLE_MAT_WINDOW;

    if (material == 1500)
        return VEHICLE_MAT_PAINT;

    if (material == 1000)
        return VEHICLE_MAT_CARBON;

    if (material == 1001)
        return VEHICLE_MAT_UNDERBODY;

    if (material == 9999)
        return VEHICLE_MAT_SPECIAL_DYNAMIC;

    if (renderer == N3P_RENDER_GOURAUD)
        return VEHICLE_MAT_GOURAUD;

    if (has_texture)
        return VEHICLE_MAT_TEXTURED;

    return VEHICLE_MAT_UNTEXTURED;
}

const char *vehicle_material_class_name(VehicleMaterialClass cls)
{
    switch (cls) {
    case VEHICLE_MAT_PAINT: return "PAINT";
    case VEHICLE_MAT_TEXTURED: return "TEXTURED";
    case VEHICLE_MAT_WINDOW: return "WINDOW";
    case VEHICLE_MAT_CARBON: return "CARBON";
    case VEHICLE_MAT_UNDERBODY: return "UNDERBODY";
    case VEHICLE_MAT_GOURAUD: return "GOURAUD";
    case VEHICLE_MAT_SPECIAL_DYNAMIC: return "SPECIAL_DYNAMIC";
    default: return "UNTEXTURED";
    }
}

VehicleUVMode vehicle_material_uv_mode(VehicleMaterialClass cls)
{
    switch (cls) {
    case VEHICLE_MAT_CARBON:
        return VEHICLE_UV_REPEAT;
    case VEHICLE_MAT_TEXTURED:
    case VEHICLE_MAT_WINDOW:
    case VEHICLE_MAT_UNDERBODY:
        return VEHICLE_UV_CLAMP;
    default:
        return VEHICLE_UV_NONE;
    }
}
