#include "scene_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared texture identity is independent of section-local material indices. */
static struct CacheEntry { char path[112]; C3D_Tex tex; unsigned refs; } cache[256];
static size_t cache_bytes;
static C3D_Tex *acquire(const SceneMaterial *m)
{
    int slot=-1;
    for(unsigned i=0;i<256;i++) {
        if(cache[i].refs && !strcmp(cache[i].path,m->texture)) {
            if(cache[i].tex.width!=m->width||cache[i].tex.height!=m->height) return NULL;
            cache[i].refs++;return &cache[i].tex;
        }
        if(!cache[i].refs && slot<0) slot=(int)i;
    }
    size_t bytes=(size_t)m->width*m->height*4;
    if(slot<0||bytes>SCENE_TEXTURE_BUDGET-cache_bytes) return NULL;
    char path[160];snprintf(path,sizeof(path),"romfs:/world/%s",m->texture);
    C3D_Tex *t=&cache[slot].tex;
    FILE *f=fopen(path,"rb");if(!f) return NULL;
    if(fseek(f,0,SEEK_END)||ftell(f)!=(long)bytes||fseek(f,0,SEEK_SET)) { fclose(f);return NULL; }
    if(!C3D_TexInit(t,m->width,m->height,GPU_RGBA8)) { fclose(f);return NULL; }
    if(fread(t->data,1,bytes,f)!=bytes) { fclose(f);C3D_TexDelete(t);return NULL; }
    fclose(f); C3D_TexFlush(t);
    C3D_TexSetFilter(t,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(t,GPU_REPEAT,GPU_REPEAT);
    strcpy(cache[slot].path,m->texture);cache[slot].refs=1;cache_bytes+=bytes;
    return t;
}
static void release(C3D_Tex *t)
{
    for(unsigned i=0;i<256;i++) if(cache[i].refs && &cache[i].tex==t) {
        if(--cache[i].refs==0) { cache_bytes-=t->size;C3D_TexDelete(t);memset(&cache[i],0,sizeof(cache[i])); }
        return;
    }
}
void scene_asset_free(SceneAsset *a)
{
    if(!a) return;
    if(a->section && a->textures) for(uint32_t i=0;i<a->section->material_count;i++) if(a->textures[i]) release(a->textures[i]);
    free(a->textures);free(a->order);scene_free(a->section);memset(a,0,sizeof(*a));
}
int scene_asset_load(SceneAsset *out,const char *path)
{
    if(!out||out->section||out->textures||out->order) return -1;
    SceneAsset next={0};next.section=scene_load(path);
    if(!next.section) return -1;
    next.textures=calloc(next.section->material_count,sizeof(*next.textures));
    next.order=calloc(next.section->batch_count,sizeof(*next.order));
    if(!next.textures||!next.order) goto bad;
    for(uint32_t i=0;i<next.section->material_count;i++) {
        next.textures[i]=acquire(&next.section->materials[i]);
        if(!next.textures[i]) goto bad;
    }
    *out=next;return 0;
bad:scene_asset_free(&next);return -1;
}
