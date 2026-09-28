#include "resources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

int resources_init(void)
{
    return 0;
}

void resources_shutdown(void)
{
}

void *resource_load(const char *path, size_t *out_size)
{
    if (out_size) *out_size = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0 || sz > 16*1024*1024) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    void *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    if (out_size)
        *out_size = (size_t)sz;
    return buf;
}

int resource_load_tex_with_subtex(const char *path, void *c3d_tex, TextureUV *out_uv,
                                  void *out_subtexture)
{
    if (!path || !c3d_tex || !out_uv)
        return -1;

    C3D_Tex *tex = (C3D_Tex *)c3d_tex;
    memset(tex, 0, sizeof(*tex));
    texture_uv_identity(out_uv);

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    Tex3DS_Texture t3x = Tex3DS_TextureImportStdio(fp, tex, NULL, false);
    fclose(fp);
    if (!t3x)
        return -1;

    /*
     * tex3ds may pad/atlas/rotate the logical image.  C3D_Tex only owns the
     * backing texture; the subtexture metadata must be copied before freeing
     * the Tex3DS_Texture handle.
     */
    if (Tex3DS_GetNumSubTextures(t3x) < 1) {
        Tex3DS_TextureFree(t3x);
        C3D_TexDelete(tex);
        memset(tex, 0, sizeof(*tex));
        return -1;
    }

    const Tex3DS_SubTexture *sub = Tex3DS_GetSubTexture(t3x, 0);
    if (!sub) {
        Tex3DS_TextureFree(t3x);
        C3D_TexDelete(tex);
        memset(tex, 0, sizeof(*tex));
        return -1;
    }

    if (out_subtexture)
        memcpy(out_subtexture, sub, sizeof(*sub));
    float tl_u, tl_v, tr_u, tr_v, bl_u, bl_v;
    Tex3DS_SubTextureTopLeft(sub, &tl_u, &tl_v);
    Tex3DS_SubTextureTopRight(sub, &tr_u, &tr_v);
    Tex3DS_SubTextureBottomLeft(sub, &bl_u, &bl_v);
    texture_uv_from_corners(out_uv, tl_u, tl_v, tr_u, tr_v, bl_u, bl_v);

    Tex3DS_TextureFree(t3x);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    return 0;
}

int resource_load_tex(const char *path, void *c3d_tex, TextureUV *out_uv)
{
    return resource_load_tex_with_subtex(path, c3d_tex, out_uv, NULL);
}
