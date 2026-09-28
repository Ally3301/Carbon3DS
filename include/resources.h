#pragma once

#include <stddef.h>
#include "texture_uv.h"

int resources_init(void);
void resources_shutdown(void);

/* Load a whole romfs file into a malloc buffer. Caller frees. */
void *resource_load(const char *path, size_t *out_size);

/*
 * Load a T3X into the supplied C3D_Tex storage and preserve subtexture mapping.
 * c3d_tex is a C3D_Tex* but kept opaque here so host parsers can include this
 * header without depending on the 3DS SDK.
 */
int resource_load_tex(const char *path, void *c3d_tex, TextureUV *out_uv);
/* Also copy the imported Tex3DS_SubTexture to opaque caller storage. */
int resource_load_tex_with_subtex(const char *path, void *c3d_tex, TextureUV *out_uv,
                                  void *out_subtexture);
