#pragma once

#include <citro3d.h>
#include <3ds.h>

int core_init(void);
int core_running(void);
void core_begin_frame(void);
void core_render_begin(void);
void core_end_frame(void);
void core_shutdown(void);

C3D_RenderTarget *core_top_target(void);
/* Low-resolution VRAM target used for additive emissive bloom. */
C3D_RenderTarget *core_glow_target(void);
C3D_Tex *core_glow_texture(void);
int core_create_bottom_target(void);
void core_destroy_bottom_target(void);
C3D_RenderTarget *core_bottom_target(void);
float core_dt(void);
