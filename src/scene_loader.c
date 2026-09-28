#include "scene_loader.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __3DS__
#include <3ds.h>
#else
#define linearAlloc malloc
#define linearFree free
#endif

_Static_assert(sizeof(SceneVertex)==36, "Scene vertex ABI");

static uint32_t read_u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static float f32(const uint8_t *p)
{ uint32_t n=read_u32(p); float f; memcpy(&f,&n,4); return f; }
static int text_valid(const uint8_t *p, size_t n, int path)
{
    size_t len=0;
    while(len<n && p[len]) {
        unsigned c=p[len++];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/'||(!path&&(c==':'||c=='+')))) return 0;
    }
    if (!len || len==n || (path && p[len-1]=='/') || p[0]=='/') return 0;
    for(size_t i=len;i<n;i++) if(p[i]) return 0;
    if (path && (strstr((const char*)p,"..") || strstr((const char*)p,"//"))) return 0;
    return 1;
}
void scene_free(SceneSection *s)
{
    if(!s) return;
    if(s->vertices) linearFree(s->vertices);
    if(s->indices) linearFree(s->indices);
    free(s->batches); free(s->materials); free(s);
}
SceneSection *scene_decode(const void *bytes,size_t size)
{
    const uint8_t *d=bytes;
    if(!d||size<256||size>SCENE_MAX_BYTES||memcmp(d,"N3S1",4)||read_u32(d+4)!=1||read_u32(d+8)!=size||read_u32(d+12)) return NULL;
    uint32_t nv=read_u32(d+16),ni=read_u32(d+20),nb=read_u32(d+24),nm=read_u32(d+28);
    if(!nv||nv>65535||!ni||ni>196608||ni%3||!nb||nb>1024||!nm||nm>256) return NULL;
    size_t vo=256,io=vo+nv*36u,bo=io+ni*2u,mo=bo+nb*16u;
    if(read_u32(d+32)!=vo||read_u32(d+36)!=io||read_u32(d+40)!=bo||read_u32(d+44)!=mo||mo+nm*192u!=size) return NULL;
    if(!text_valid(d+72,64,1)||!text_valid(d+136,88,0)) return NULL;
    SceneSection *s=calloc(1,sizeof(*s));
    if(!s) return NULL;
    s->vertex_count=nv;s->index_count=ni;s->batch_count=nb;s->material_count=nm;
    memcpy(s->section,d+72,64);memcpy(s->source,d+136,88);memcpy(s->normalized_sha256,d+224,32);
    for(unsigned k=0;k<6;k++) {
        s->bounds[k]=f32(d+48+4*k);
        if(!isfinite(s->bounds[k])||fabsf(s->bounds[k])>1e7f) goto bad;
    }
    for(unsigned k=0;k<3;k++) if(s->bounds[k]>s->bounds[k+3]) goto bad;
    s->vertices=linearAlloc(nv*sizeof(*s->vertices));s->indices=linearAlloc(ni*sizeof(*s->indices));
    s->batches=calloc(nb,sizeof(*s->batches));s->materials=calloc(nm,sizeof(*s->materials));
    if(!s->vertices||!s->indices||!s->batches||!s->materials) goto bad;
    s->geometry_bytes=nv*sizeof(*s->vertices)+ni*sizeof(*s->indices)+nb*sizeof(*s->batches)+nm*sizeof(*s->materials)+sizeof(*s);
    for(uint32_t i=0;i<nv;i++) {
        float v[9];
        for(unsigned k=0;k<9;k++) { v[k]=f32(d+vo+i*36+4*k); if(!isfinite(v[k])) goto bad; }
        for(unsigned k=0;k<3;k++) if(v[k]<s->bounds[k]||v[k]>s->bounds[k+3]) goto bad;
        for(unsigned k=5;k<9;k++) if(v[k]<0||v[k]>1) goto bad;
        memcpy(&s->vertices[i],v,sizeof(v));

        /* Palmont CDL stores texture coordinates as signed fixed-point values.
           The recovered stream uses 256 units per UV unit (8.8 fixed-point scale);
           feeding those raw shorts to PICA200 as floats makes a coordinate such
           as 8191 behave like 8191 texture repeats instead of ~31.996.  The result
           is the dense/noisy texture pattern seen on hardware even though the
           geometry itself is correct.

           Keep N3S generic: PocketGarage and other normalized scenes already
           contain conventional 0..N UVs.  Only the recovered opwd_3000 world
           sections need this conversion. */
        if (!strncmp(s->section, "opwd_3000/", 10)) {
            s->vertices[i].uv[0] *= (1.0f / 256.0f);
            s->vertices[i].uv[1] *= (1.0f / 256.0f);
        }
    }
    for(uint32_t i=0;i<ni;i++) {
        const uint8_t *p=d+io+i*2; s->indices[i]=p[0]|(uint16_t)p[1]<<8;
        if(s->indices[i]>=nv) goto bad;
    }
    uint32_t covered=0;
    for(uint32_t i=0;i<nb;i++) {
        const uint8_t *p=d+bo+i*16; SceneBatch *b=&s->batches[i];
        b->first=read_u32(p);b->count=read_u32(p+4);b->material=read_u32(p+8);
        if(b->first!=covered||!b->count||b->count%3||b->count>ni-covered||b->material>=nm||read_u32(p+12)) goto bad;
        for(uint32_t j=0;j<b->count;j++) for(unsigned k=0;k<3;k++) b->center[k]+=s->vertices[s->indices[b->first+j]].pos[k]/b->count;
        covered+=b->count;
    }
    if(covered!=ni) goto bad;
    for(uint32_t i=0;i<nm;i++) {
        const uint8_t *p=d+mo+i*192; SceneMaterial *m=&s->materials[i];
        if(!text_valid(p,64,1)||!text_valid(p+64,112,1)||read_u32(p+188)) goto bad;
        memcpy(m->key,p,64);memcpy(m->texture,p+64,112);
        m->alpha_mode=read_u32(p+176);m->width=read_u32(p+180);m->height=read_u32(p+184);
        if(m->alpha_mode>3||m->width<8||m->width>1024||m->height<8||m->height>1024||(m->width&(m->width-1))||(m->height&(m->height-1))) goto bad;
        int shared=0;
        for(uint32_t j=0;j<i;j++) {
            SceneMaterial *prev=&s->materials[j];
            if(!strcmp(prev->key,m->key)) goto bad;
            if(!strcmp(prev->texture,m->texture)) {
                if(prev->width!=m->width||prev->height!=m->height) goto bad;
                shared=1;
            }
        }
        if(!shared) s->texture_bytes+=(size_t)m->width*m->height*4;
        if(s->texture_bytes>SCENE_TEXTURE_BUDGET) goto bad;
    }
#ifdef __3DS__
    GSPGPU_FlushDataCache(s->vertices,nv*sizeof(*s->vertices));
    GSPGPU_FlushDataCache(s->indices,ni*sizeof(*s->indices));
#endif
    return s;
bad: scene_free(s);return NULL;
}
SceneSection *scene_load(const char *path)
{
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    if(fseek(f,0,SEEK_END)) { fclose(f);return NULL; }
    long n=ftell(f);
    if(n<256||n>(long)SCENE_MAX_BYTES||fseek(f,0,SEEK_SET)) { fclose(f);return NULL; }
    void *data=malloc((size_t)n); SceneSection *s=NULL;
    if(data && fread(data,1,(size_t)n,f)==(size_t)n) s=scene_decode(data,(size_t)n);
    free(data);fclose(f);return s;
}
