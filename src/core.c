#include "core.h"

#include <stdio.h>
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>

#define CLEAR_COLOR 0x101820FF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C3D_RenderTarget *s_glow;
static C3D_Tex s_glow_tex;
static TickCounter s_tick;
static float s_dt = 1.0f / 60.0f;
static int s_quit;

int core_init(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { gfxExit(); return -1; }
    if (R_FAILED(romfsInit())) { C3D_Fini(); gfxExit(); return -1; }

    s_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!s_top) { romfsExit(); C3D_Fini(); gfxExit(); return -1; }
    C3D_RenderTargetSetOutput(s_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    /* 256x128 RGBA8 = 128 KiB in VRAM.  No depth buffer is needed because
       this target contains only emissive billboards, then gets upsampled. */
    if (C3D_TexInitVRAM(&s_glow_tex, 256, 128, GPU_RGBA8)) {
        C3D_TexSetFilter(&s_glow_tex, GPU_LINEAR, GPU_LINEAR);
        s_glow = C3D_RenderTargetCreateFromTex(&s_glow_tex, GPU_TEXFACE_2D, 0, -1);
    }

    consoleInit(GFX_BOTTOM, NULL);
    osTickCounterStart(&s_tick);
    s_quit = 0;
    return 0;
}

int core_running(void)
{
    return aptMainLoop() && !s_quit;
}

void core_begin_frame(void)
{
    osTickCounterUpdate(&s_tick);
    s_dt = (float)osTickCounterRead(&s_tick) / 1000.0f;
    if (s_dt <= 0.0f || s_dt > 0.1f)
        s_dt = 1.0f / 60.0f;

    hidScanInput();
    const u32 quit_chord = KEY_L | KEY_R | KEY_SELECT;
    if ((hidKeysHeld() & quit_chord) == quit_chord)
        s_quit = 1;

}

void core_render_begin(void)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(s_top, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
    C3D_FrameDrawOn(s_top);
}

void core_end_frame(void)
{
    C3D_FrameEnd(0);
}

void core_shutdown(void)
{
    core_destroy_bottom_target();
    if (s_glow) C3D_RenderTargetDelete(s_glow);
    if (s_glow_tex.data) C3D_TexDelete(&s_glow_tex);
    C3D_RenderTargetDelete(s_top);
    romfsExit();
    C3D_Fini();
    gfxExit();
}

C3D_RenderTarget *core_top_target(void)
{
    return s_top;
}
C3D_RenderTarget *core_glow_target(void)
{
    return s_glow;
}

C3D_Tex *core_glow_texture(void)
{
    return s_glow ? &s_glow_tex : NULL;
}

int core_create_bottom_target(void)
{
    if (s_bottom) return 0;
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    return s_bottom ? 0 : -1;
}

void core_destroy_bottom_target(void)
{
    if (s_bottom) {
        C3D_RenderTargetDelete(s_bottom);
        s_bottom = NULL;
    }
}

C3D_RenderTarget *core_bottom_target(void)
{
    return s_bottom;
}

float core_dt(void)
{
    return s_dt;
}
