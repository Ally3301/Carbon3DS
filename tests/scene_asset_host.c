/* Exercise the real resource ownership code; GPU storage is mocked on host. */
#include "scene_asset.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static size_t live_bytes;
static int fail_after=-1,live_count;
bool C3D_TexInit(C3D_Tex *t,uint16_t w,uint16_t h,int fmt)
{
    (void)fmt;
    if(fail_after==0) return false;
    if(fail_after>0) fail_after--;
    t->size=(size_t)w*h*4;t->data=malloc(t->size);t->width=w;t->height=h;
    if(!t->data) return false;
    live_bytes+=t->size;live_count++;return true;
}
void C3D_TexDelete(C3D_Tex *t) { assert(t->data);live_bytes-=t->size;live_count--;free(t->data);t->data=NULL; }
void C3D_TexSetFilter(C3D_Tex *t,int a,int b) { (void)t;(void)a;(void)b; }
void C3D_TexSetWrap(C3D_Tex *t,int a,int b) { (void)t;(void)a;(void)b; }
void C3D_TexFlush(C3D_Tex *t) { (void)t; }
int main(int argc,char **argv)
{
    (void)argv;
    SceneAsset a={0},b={0};
    if(argc>1) {
        assert(scene_asset_load(&a,"romfs:/world/garage.n3s")==-1);
        assert(!a.section && !a.textures && !a.order && !live_count && !live_bytes);
        return 0;
    }
    for(int cycle=0;cycle<32;cycle++) {
        assert(scene_asset_load(&a,"romfs:/world/garage.n3s")==0);
        size_t bytes=live_bytes;int count=live_count;
        assert(bytes==a.section->texture_bytes && count==42);
        assert(scene_asset_load(&b,"romfs:/world/garage.n3s")==0);
        assert(live_bytes==bytes && live_count==count);
        scene_asset_free(&a);assert(live_bytes==bytes);
        scene_asset_free(&b);assert(!live_bytes && !live_count);
        scene_asset_free(&b);
    }
    for(int failure=0;failure<42;failure++) {
        fail_after=failure;
        assert(scene_asset_load(&a,"romfs:/world/garage.n3s")==-1);
        assert(!a.section && !a.textures && !a.order && !live_count && !live_bytes);
    }
    puts("Scene cache: 32 shared load/unload cycles and 42 allocation failures clean");
    return 0;
}
