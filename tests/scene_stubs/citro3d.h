#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { void *data; size_t size; uint16_t width,height; } C3D_Tex;
enum { GPU_RGBA8, GPU_LINEAR, GPU_REPEAT };
bool C3D_TexInit(C3D_Tex *,uint16_t,uint16_t,int);
void C3D_TexDelete(C3D_Tex *);
void C3D_TexSetFilter(C3D_Tex *,int,int);
void C3D_TexSetWrap(C3D_Tex *,int,int);
void C3D_TexFlush(C3D_Tex *);
