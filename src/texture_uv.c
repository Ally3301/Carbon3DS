#include "texture_uv.h"

void texture_uv_identity(TextureUV *uv)
{
    if (!uv)
        return;
    uv->origin[0] = 0.0f;
    uv->origin[1] = 0.0f;
    uv->axis_u[0] = 1.0f;
    uv->axis_u[1] = 0.0f;
    uv->axis_v[0] = 0.0f;
    uv->axis_v[1] = 1.0f;
}

void texture_uv_from_corners(TextureUV *uv,
                             float tl_u, float tl_v,
                             float tr_u, float tr_v,
                             float bl_u, float bl_v)
{
    if (!uv)
        return;
    uv->origin[0] = tl_u;
    uv->origin[1] = tl_v;
    uv->axis_u[0] = tr_u - tl_u;
    uv->axis_u[1] = tr_v - tl_v;
    uv->axis_v[0] = bl_u - tl_u;
    uv->axis_v[1] = bl_v - tl_v;
}

void texture_uv_apply(const TextureUV *uv, float u, float v,
                      float *out_u, float *out_v)
{
    if (!uv || !out_u || !out_v)
        return;
    *out_u = uv->origin[0] + uv->axis_u[0] * u + uv->axis_v[0] * v;
    *out_v = uv->origin[1] + uv->axis_u[1] * u + uv->axis_v[1] * v;
}
