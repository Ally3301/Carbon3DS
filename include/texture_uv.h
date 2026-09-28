#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Affine mapping from logical source UVs into a texture/subtexture.
 *
 * Logical convention used by imported game/UI images:
 *   (0,0) = top-left
 *   (1,0) = top-right
 *   (0,1) = bottom-left
 *
 * The mapping also supports tex3ds' 90-degree rotated atlas entries.
 */
typedef struct TextureUV {
    float origin[2];
    float axis_u[2];
    float axis_v[2];
} TextureUV;

void texture_uv_identity(TextureUV *uv);
void texture_uv_from_corners(TextureUV *uv,
                             float tl_u, float tl_v,
                             float tr_u, float tr_v,
                             float bl_u, float bl_v);
void texture_uv_apply(const TextureUV *uv, float u, float v,
                      float *out_u, float *out_v);

#ifdef __cplusplus
}
#endif
