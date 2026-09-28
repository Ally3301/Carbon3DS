#include "renderer.h"
#include "hud_layout.h"
#include "core.h"
#include "track.h"
#include "resources.h"
#include "vehicle_material_policy.h"
#include "gpu_color.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <tex3ds.h>
#include "vshader_shbin.h"
#include "scene_shader_shbin.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ROAD_SEGMENTS 128
#define WHEEL_SEGMENTS 32

static DVLB_s *s_dvlb, *s_scene_dvlb;
static shaderProgram_s s_scene_program;
static int s_scene_proj, s_scene_mv, s_scene_tex_flip;
static shaderProgram_s s_program;
static int s_u_proj, s_u_mv, s_u_light, s_u_half, s_u_lclr, s_u_mat;
static int s_u_uv_origin, s_u_uv_u, s_u_uv_v;
static C3D_Mtx s_proj;
static C3D_Mtx s_material;
static C3D_Mtx s_paint_material;
static C3D_Mtx s_window_material;
static C3D_Mtx s_clearcoat_material;
static N3PVertex *s_ground;
static N3PVertex *s_road;
static int s_road_count;
static N3PVertex *s_wheel_faces, *s_wheel_rim_faces, *s_wheel_tread;
static N3PPart *s_wheel_tire_mesh;
static N3PVertex *s_wheel_stream;
static uint32_t s_wheel_tire_offset, s_wheel_rim_offset, s_wheel_stream_count;
static N3PVertex *s_vehicle_stream;
static size_t s_vehicle_stream_capacity;
/* A command list reads buffers asynchronously. Each visible garage neighbour
   therefore owns a stream for the entire frame instead of sharing the active
   car's single stream. */
static N3PVertex *s_garage_stream[4];
static size_t s_garage_stream_capacity[4];
static uint32_t s_vehicle_stream_base[VEHICLE_SLOT_COUNT];
static int s_wheel_face_count, s_wheel_tread_count;
static C3D_Tex s_garage_icons[5];
static TextureUV s_garage_icon_uv[5];
static int s_garage_icon_count;
static C3D_Tex s_performance_icons[5];
static TextureUV s_performance_icon_uv[5];
static int s_performance_icon_count;
static C3D_Tex s_garage_car_icon;
static TextureUV s_garage_car_icon_uv;
static int s_garage_car_icon_loaded;
static C3D_Tex s_frontend_textures[6];
static TextureUV s_frontend_texture_uv[6];
static int s_frontend_texture_count;
static C3D_Tex s_effect_textures[3];
static TextureUV s_effect_texture_uv[3];
static int s_effect_texture_count;
static C3D_Tex s_hud_textures[8];
static TextureUV s_hud_texture_uv[8];
static int s_hud_texture_count;
static C3D_Tex s_gps_player;
static TextureUV s_gps_player_uv;
static C3D_Tex s_sky_texture;
static TextureUV s_sky_uv;
static int s_sky_loaded;
static Tex3DS_SubTexture s_gps_player_subtex;
static int s_gps_player_loaded;
/* The map is a local camera, not a car-centred decal.  It is retained until
   the player nears the rim so the marker can travel across the streets. */
static float s_minimap_origin_x, s_minimap_origin_z;
static int s_minimap_origin_valid;
static int s_minimap_matched_link=-1;
static TextureUV s_glow_uv;
#define UI_QUAD_POOL 96
static SceneVertex *s_ui_quad;
static unsigned s_ui_quad_cursor;
static N3PVertex *s_effect_stream;
static float s_effect_time;
static C2D_TextBuf s_text_buffer;
static int s_c2d_initialized;
static int s_vehicle_debug;
static void bind_vehicle_program(void);
static void begin_ui(C3D_Mtx *projection, C3D_Mtx *identity);
static void ui_quad(float x, float y, float w, float h, u32 rgba,
                    C3D_Tex *texture, const TextureUV *uv);

static int make_wheel(void)
{
    /* The recovered 64x64 wheel atlas reserves its lower-left circle for
       the visible alloy rim. Its top band and surrounding pixels describe
       tyre data, so sampling the full PNG turns the rim into a billboard. */
    const float face_cx = 23.5f / 64.0f;
    const float face_cy = 39.5f / 64.0f;
    const float face_r  = 22.5f / 64.0f;

    s_wheel_face_count = WHEEL_SEGMENTS * 6;
    /* Four quads per segment: outer tread, front/back sidewalls and inner
       bead. Unlike the former opaque disc this leaves a real hole for rim. */
    s_wheel_tread_count = WHEEL_SEGMENTS * 24;
    s_wheel_faces = linearAlloc(sizeof(N3PVertex) * s_wheel_face_count);
    s_wheel_rim_faces = linearAlloc(sizeof(N3PVertex) * s_wheel_face_count);
    s_wheel_tread = linearAlloc(sizeof(N3PVertex) * s_wheel_tread_count);
    if (!s_wheel_faces || !s_wheel_rim_faces || !s_wheel_tread)
        return -1;

    for (int i = 0; i < WHEEL_SEGMENTS; ++i) {
        float t0 = (float)i / (float)WHEEL_SEGMENTS;
        float t1 = (float)(i + 1) / (float)WHEEL_SEGMENTS;
        float a = t0 * 2.0f * (float)M_PI;
        float b = t1 * 2.0f * (float)M_PI;
        float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
        float uca = face_cx + ca * face_r, vca = face_cy + sa * face_r;
        float ucb = face_cx + cb * face_r, vcb = face_cy + sb * face_r;

        /* Alloy rim: a small textured disc at each wheel exterior. */
        N3PVertex rim[6] = {
            {{0,0,-0.54f},{face_cx,face_cy},{0,0,-1}},
            {{ca*.72f,sa*.72f,-0.54f},{uca,vca},{0,0,-1}},
            {{cb*.72f,sb*.72f,-0.54f},{ucb,vcb},{0,0,-1}},
            {{0,0, 0.54f},{face_cx,face_cy},{0,0,1}},
            {{cb*.72f,sb*.72f, 0.54f},{ucb,vcb},{0,0,1}},
            {{ca*.72f,sa*.72f, 0.54f},{uca,vca},{0,0,1}},
        };
        memcpy(s_wheel_rim_faces + i*6, rim, sizeof(rim));

        /* Tyre ring: outer radius 1.0, rim opening .72, axial half-width .5.
           This is geometry, not a black sprite behind the rim. */
        const float ri = .72f, zo = .50f;
        N3PVertex tyre[24] = {
            /* outer tread */
            {{ca,sa,-zo},{0,0},{ca,sa,0}}, {{ca,sa,zo},{0,1},{ca,sa,0}},
            {{cb,sb,zo},{1,1},{cb,sb,0}}, {{ca,sa,-zo},{0,0},{ca,sa,0}},
            {{cb,sb,zo},{1,1},{cb,sb,0}}, {{cb,sb,-zo},{1,0},{cb,sb,0}},
            /* positive sidewall */
            {{ca,sa,zo},{0,0},{0,0,1}}, {{ca*ri,sa*ri,zo},{0,1},{0,0,1}},
            {{cb*ri,sb*ri,zo},{1,1},{0,0,1}}, {{ca,sa,zo},{0,0},{0,0,1}},
            {{cb*ri,sb*ri,zo},{1,1},{0,0,1}}, {{cb,sb,zo},{1,0},{0,0,1}},
            /* negative sidewall */
            {{ca*ri,sa*ri,-zo},{0,1},{0,0,-1}}, {{ca,sa,-zo},{0,0},{0,0,-1}},
            {{cb,sb,-zo},{1,0},{0,0,-1}}, {{ca*ri,sa*ri,-zo},{0,1},{0,0,-1}},
            {{cb,sb,-zo},{1,0},{0,0,-1}}, {{cb*ri,sb*ri,-zo},{1,1},{0,0,-1}},
            /* inner bead, visible through open-spoke rims */
            {{ca*ri,sa*ri,-zo},{0,0},{-ca,-sa,0}}, {{ca*ri,sa*ri,zo},{0,1},{-ca,-sa,0}},
            {{cb*ri,sb*ri,zo},{1,1},{-cb,-sb,0}}, {{ca*ri,sa*ri,-zo},{0,0},{-ca,-sa,0}},
            {{cb*ri,sb*ri,zo},{1,1},{-cb,-sb,0}}, {{cb*ri,sb*ri,-zo},{1,0},{-cb,-sb,0}},
        };
        memcpy(s_wheel_tread + i*24, tyre, sizeof(tyre));
    }
    GSPGPU_FlushDataCache(s_wheel_faces, sizeof(N3PVertex) * s_wheel_face_count);
    GSPGPU_FlushDataCache(s_wheel_rim_faces, sizeof(N3PVertex) * s_wheel_face_count);
    GSPGPU_FlushDataCache(s_wheel_tread, sizeof(N3PVertex) * s_wheel_tread_count);
    return 0;
}

static int make_ground(void)
{
    const float size = 220.0f;
    N3PVertex q[4] = {
        {{-size,-0.04f,-size},{0,0},{0,1,0}},
        {{ size,-0.04f,-size},{0,0},{0,1,0}},
        {{ size,-0.04f, size},{0,0},{0,1,0}},
        {{-size,-0.04f, size},{0,0},{0,1,0}},
    };
    s_ground = linearAlloc(sizeof(N3PVertex) * 6);
    s_road = linearAlloc(sizeof(N3PVertex) * ROAD_SEGMENTS * 24);
    if (!s_ground || !s_road)
        return -1;

    const int order[6] = {0,1,2,0,2,3};
    for (int j = 0; j < 6; ++j)
        s_ground[j] = q[order[j]];

    for (int band = 0; band < 4; ++band) {
        float r0 = TRACK_RADIUS - TRACK_WIDTH/2;
        float r1 = TRACK_RADIUS + TRACK_WIDTH/2;
        float y = 0;
        if (band == 1) { r1 = r0 + 0.6f; y = 0.02f; }
        if (band == 2) { r0 = r1 - 0.6f; y = 0.02f; }
        if (band == 3) { r0 = TRACK_RADIUS - 0.12f; r1 = TRACK_RADIUS + 0.12f; y = 0.03f; }

        for (int i = 0; i < ROAD_SEGMENTS; ++i) {
            float a = i * 2 * M_PI / ROAD_SEGMENTS;
            float b = (i+1) * 2 * M_PI / ROAD_SEGMENTS;
            float aa[4] = {a,a,b,b};
            float rr[4] = {r0,r1,r1,r0};
            for (int k = 0; k < 4; ++k)
                q[k] = (N3PVertex){{rr[k]*cosf(aa[k]),y,rr[k]*sinf(aa[k])},{0,0},{0,1,0}};
            for (int j = 0; j < 6; ++j)
                s_road[s_road_count++] = q[order[j]];
        }
    }

    GSPGPU_FlushDataCache(s_ground, sizeof(N3PVertex) * 6);
    GSPGPU_FlushDataCache(s_road, sizeof(N3PVertex) * s_road_count);
    return 0;
}

int renderer_init(void)
{
    s_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    if (!s_dvlb)
        return -1;

    shaderProgramInit(&s_program);
    shaderProgramSetVsh(&s_program, &s_dvlb->DVLE[0]);
    C3D_BindProgram(&s_program);

    s_u_proj  = shaderInstanceGetUniformLocation(s_program.vertexShader, "projection");
    s_u_mv    = shaderInstanceGetUniformLocation(s_program.vertexShader, "modelView");
    s_u_light = shaderInstanceGetUniformLocation(s_program.vertexShader, "lightVec");
    s_u_half  = shaderInstanceGetUniformLocation(s_program.vertexShader, "lightHalfVec");
    s_u_lclr  = shaderInstanceGetUniformLocation(s_program.vertexShader, "lightClr");
    s_u_mat   = shaderInstanceGetUniformLocation(s_program.vertexShader, "material");
    s_u_uv_origin = shaderInstanceGetUniformLocation(s_program.vertexShader, "texUvOrigin");
    s_u_uv_u      = shaderInstanceGetUniformLocation(s_program.vertexShader, "texUvU");
    s_u_uv_v      = shaderInstanceGetUniformLocation(s_program.vertexShader, "texUvV");

    s_material = (C3D_Mtx){{
        { { 0.0f, 0.18f, 0.18f, 0.20f } },
        { { 0.0f, 0.62f, 0.62f, 0.62f } },
        { { 0.0f, 0.38f, 0.38f, 0.38f } },
        { { 1.0f, 0.04f, 0.04f, 0.05f } },
    }};
    /* Painted panels receive a sharper PICA vertex-specular response.  This
       is intentionally a material variant, leaving decals, tyres and glass
       on the conservative lighting that was already hardware validated. */
    s_paint_material = (C3D_Mtx){{
        { { 0.0f, 0.16f, 0.18f, 0.22f } },
        { { 0.0f, 0.60f, 0.64f, 0.68f } },
        { { 0.0f, 0.70f, 0.70f, 0.72f } },
        { { 1.0f, 0.06f, 0.07f, 0.09f } },
    }};
    /* Glass keeps the strong edge response.  Paint is lit by its regular
       specular lobe so it reads as clearcoat rather than frosted glass. */
    s_window_material = (C3D_Mtx){{
        { { 0.0f, 0.07f, 0.10f, 0.14f } },
        { { 0.0f, 0.22f, 0.28f, 0.36f } },
        { { 0.0f, 0.72f, 0.82f, 0.96f } },
        { { 1.0f, 0.03f, 0.05f, 0.10f } },
    }};
    /* Clearcoat has no diffuse/albedo term: the second PAINT pass carries
       only a restrained blue-white reflection over the original body colour. */
    s_clearcoat_material = (C3D_Mtx){{
        { { 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.20f, 0.24f, 0.30f } },
        { { 0.0f, 0.0f, 0.0f, 0.0f } },
    }};

    texture_uv_identity(&s_glow_uv);
    C3D_AttrInfo *attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 3);

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

    s_scene_dvlb = DVLB_ParseFile((u32 *)scene_shader_shbin, scene_shader_shbin_size);
    if (!s_scene_dvlb) { renderer_shutdown(); return -1; }
    shaderProgramInit(&s_scene_program);
    shaderProgramSetVsh(&s_scene_program, &s_scene_dvlb->DVLE[0]);
    s_scene_proj = shaderInstanceGetUniformLocation(s_scene_program.vertexShader, "projection");
    s_scene_mv = shaderInstanceGetUniformLocation(s_scene_program.vertexShader, "modelView");
    s_scene_tex_flip = shaderInstanceGetUniformLocation(s_scene_program.vertexShader, "texFlip");
    if (make_ground() ||
        !(s_wheel_tire_mesh = n3p_load("romfs:/wheels/tire_low_poly.n3p"))) {
        renderer_shutdown();
        return -1;
    }
    /* PSP WHEEL_STOCK already carries tyre, rim and caliper packets. */
    s_wheel_tire_offset = 0;
    s_wheel_rim_offset = 0;
    s_wheel_stream_count = s_wheel_tire_mesh->draw_vertex_count;
    s_wheel_stream = linearAlloc((size_t)s_wheel_stream_count * sizeof(*s_wheel_stream));
    if (!s_wheel_stream) { renderer_shutdown(); return -1; }
    memcpy(s_wheel_stream + s_wheel_tire_offset, s_wheel_tire_mesh->draw_verts,
           (size_t)s_wheel_tire_mesh->draw_vertex_count * sizeof(*s_wheel_stream));
    GSPGPU_FlushDataCache(s_wheel_stream,
                          (size_t)s_wheel_stream_count * sizeof(*s_wheel_stream));
    static const char *const icons[] = {
        "bodykits", "hoods", "spoilers", "paint", "rims"
    };
    s_ui_quad = linearAlloc(sizeof(SceneVertex) * 6 * UI_QUAD_POOL);
    s_effect_stream = linearAlloc(sizeof(N3PVertex) * 138);
    if (!s_ui_quad || !s_effect_stream) {
        renderer_shutdown();
        return -1;
    }
    for (unsigned i = 0; i < sizeof(icons)/sizeof(icons[0]); ++i) {
        char path[96];
        snprintf(path, sizeof(path), "romfs:/ui/garage/%s.t3x", icons[i]);
        if (resource_load_tex(path, &s_garage_icons[i], &s_garage_icon_uv[i]) != 0) {
            renderer_shutdown();
            return -1;
        }
        C3D_TexSetWrap(&s_garage_icons[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_garage_icon_count++;
    }
    static const char *const performance_icons[] = {"engine", "turbo", "chassis", "handling", "nitrous"};
    for (unsigned i=0;i<sizeof(performance_icons)/sizeof(performance_icons[0]);++i) {
        char path[96];
        snprintf(path,sizeof(path),"romfs:/ui/garage/%s.t3x",performance_icons[i]);
        if (resource_load_tex(path, &s_performance_icons[i], &s_performance_icon_uv[i]) != 0) {
            renderer_shutdown(); return -1;
        }
        C3D_TexSetWrap(&s_performance_icons[i],GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        s_performance_icon_count++;
    }
        if (resource_load_tex("romfs:/ui/frontend/car_slot.t3x", &s_garage_car_icon,
                          &s_garage_car_icon_uv) == 0) {
        C3D_TexSetWrap(&s_garage_car_icon, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_garage_car_icon_loaded=1;
    }
    static const char *const frontend[] = {"background", "carbon_logo", "quick_background", "quick_wheel", "menu_grid_ai", "menu_selection_tight_ai"};
    for (unsigned i = 0; i < sizeof(frontend)/sizeof(frontend[0]); ++i) {
        char path[96];
        snprintf(path, sizeof(path), "romfs:/ui/frontend/%s.t3x", frontend[i]);
        if (resource_load_tex(path, &s_frontend_textures[i], &s_frontend_texture_uv[i]) != 0) {
            renderer_shutdown();
            return -1;
        }
        C3D_TexSetWrap(&s_frontend_textures[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_frontend_texture_count++;
    }
    static const char *const effects[] = {"headlight_bloom", "taillight_bloom", "tire_smoke"};
    for (unsigned i=0;i<sizeof(effects)/sizeof(effects[0]);++i) {
        char path[96]; snprintf(path,sizeof(path),"romfs:/effects/%s.t3x",effects[i]);
        if (resource_load_tex(path,&s_effect_textures[i],&s_effect_texture_uv[i]) != 0) {
            renderer_shutdown(); return -1;
        }
        C3D_TexSetWrap(&s_effect_textures[i],GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        s_effect_texture_count++;
    }
    static const char *const hud[] = {"tach_face", "tach_redline", "tach_needle",
                                      "tach_gear_digits", "tach_speed_digits", "tach_nitro",
                                      "tach_nitro_icon", "hud_minimap_bezel_ai"};
    for (unsigned i=0;i<8;i++) {
        char path[96]; snprintf(path,sizeof(path),"romfs:/ui/hud/%s.t3x",hud[i]);
        if (resource_load_tex(path,&s_hud_textures[i],&s_hud_texture_uv[i]) != 0) { renderer_shutdown(); return -1; }
        C3D_TexSetWrap(&s_hud_textures[i],GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE); s_hud_texture_count++;
    }
    if (resource_load_tex_with_subtex("romfs:/ui/hud/gps_player.t3x", &s_gps_player,
                                      &s_gps_player_uv, &s_gps_player_subtex) == 0) {
        C3D_TexSetWrap(&s_gps_player, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_gps_player_loaded = 1;
    }
    if (resource_load_tex("romfs:/ui/world/sky_sunset.t3x", &s_sky_texture, &s_sky_uv) == 0) {
        C3D_TexSetWrap(&s_sky_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_sky_loaded = 1;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        renderer_shutdown();
        return -1;
    }
    s_c2d_initialized = 1;
    if (core_create_bottom_target() != 0) {
        renderer_shutdown();
        return -1;
    }
    s_text_buffer = C2D_TextBufNew(512);
    if (!s_text_buffer) {
        renderer_shutdown();
        return -1;
    }
    return 0;
}

static void bind_texenv_texture(void)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
}

static void bind_texenv_vertex(void)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void bind_texenv_tint(u32 rgba)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvColor(env, rgba);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_CONSTANT, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
}

/* Vehicle-only state isolation. Scene/N3S rendering deliberately never calls
   these wrappers, preserving the already hardware-validated garage path. */
static void vehicle_reset_secondary_texenv(void)
{
    C3D_TexEnv *env1 = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env1);
}

static void bind_vehicle_texenv_texture(void)
{
    vehicle_reset_secondary_texenv();
    bind_texenv_texture();
}

/* PSP NFSCar_TextureShiny alpha is a specular/reflection mask.  It is not
   opacity: forwarding it to the blend unit erased lamp, badge and wheel RGB. */
/* Decals and lens maps carry their own RGB.  Do not modulate them with
   the PAINT vertex colour: it turned neutral badges yellow on the Supra and
   tinted reverse lamps red on painted cars. */
static void bind_vehicle_texenv_decal(void)
{
    vehicle_reset_secondary_texenv();
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

static void bind_vehicle_texenv_texture_opaque(void)
{
    vehicle_reset_secondary_texenv();
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvColor(env, gpu_rgba8(0, 0, 0, 0xFF));
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

static void bind_vehicle_texenv_vertex(void)
{
    vehicle_reset_secondary_texenv();
    bind_texenv_vertex();
}

static void bind_vehicle_texenv_tint(u32 rgba)
{
    vehicle_reset_secondary_texenv();
    bind_texenv_tint(rgba);
}

/* Native 3DS glass reconstruction.
   The recovered 1501/WINDOW texture has black RGB and useful alpha. Sampling
   its RGB literally produces black glass, so use its alpha as coverage while
   deriving glass colour from a stable PICA constant and vertex lighting. */
static void bind_texenv_window(u32 rgba, int sample_alpha)
{
    vehicle_reset_secondary_texenv();
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvColor(env, rgba);

    C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_CONSTANT, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);

    if (sample_alpha) {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    } else {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    }
}

static void set_vehicle_uv(const TextureUV *uv)
{
    TextureUV identity;
    if (!uv) {
        texture_uv_identity(&identity);
        uv = &identity;
    }
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_uv_origin,
                  uv->origin[0], uv->origin[1], 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_uv_u,
                  uv->axis_u[0], uv->axis_u[1], 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_uv_v,
                  uv->axis_v[0], uv->axis_v[1], 0.0f, 0.0f);
}

static void bind_vehicle_material(const VehicleAsset *vehicle,
                                  VehiclePartSlot slot,
                                  const N3PGroup *group)
{
    const C3D_Tex *tex = vehicle_asset_texture(vehicle, group->material);
    const TextureUV *uv = vehicle_asset_texture_uv(vehicle, group->material);
    VehicleMaterialClass cls =
        vehicle_material_classify(group->renderer, group->material, tex != NULL);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mat,
                     cls == VEHICLE_MAT_WINDOW ? &s_window_material :
                     cls == VEHICLE_MAT_PAINT ? &s_paint_material : &s_material);

    if (s_vehicle_debug && cls != VEHICLE_MAT_WINDOW) {
        static const u32 slot_color[VEHICLE_SLOT_COUNT] = {
            0xFF3030E8u, /* BODY    = red   (R low byte) */
            0xFF30D050u, /* BASE    = green */
            0xFFE85030u, /* HOOD    = blue  */
            0xFF30E8F0u, /* SPOILER = yellow */
            0xFFFF30FFu,
            0xFFFF80FFu,
        };
        set_vehicle_uv(NULL);
        bind_vehicle_texenv_tint(slot < VEHICLE_SLOT_COUNT
                                 ? slot_color[slot]
                                 : gpu_rgba8(0xFF,0xFF,0xFF,0xFF));
        return;
    }

    /* Do not translate the original EAGL state machine literally here.
       These are deliberately small, deterministic PICA200 material classes.
       jogo.c is used only to classify semantics. */
    switch (cls) {
    case VEHICLE_MAT_PAINT:
        /* The experiment that sampled *_DETAILS directly on PAINT regressed
           badly on hardware. Keep DETAILS metadata for RE, but do not sample
           it until its original coordinate/channel semantics are understood. */
        set_vehicle_uv(NULL);
        bind_vehicle_texenv_tint(vehicle_asset_paint(vehicle));
        break;

    case VEHICLE_MAT_WINDOW:
        if (tex) {
            set_vehicle_uv(uv);
            C3D_TexBind(0, (C3D_Tex *)tex);
            bind_texenv_window(gpu_rgba8(0x60,0x74,0x86,0xB8), 1);
        } else {
            set_vehicle_uv(NULL);
            bind_texenv_window(gpu_rgba8(0x60,0x74,0x86,0xB8), 0);
        }
        break;

    case VEHICLE_MAT_CARBON:
        if (tex) {
            set_vehicle_uv(uv);
            C3D_TexBind(0, (C3D_Tex *)tex);
            bind_vehicle_texenv_texture();
        } else {
            set_vehicle_uv(NULL);
            bind_vehicle_texenv_tint(gpu_rgba8(0x48,0x48,0x48,0xFF));
        }
        break;

    case VEHICLE_MAT_UNDERBODY:
        if (tex) {
            set_vehicle_uv(uv);
            C3D_TexBind(0, (C3D_Tex *)tex);
            bind_vehicle_texenv_texture();
        } else {
            set_vehicle_uv(NULL);
            bind_vehicle_texenv_tint(gpu_rgba8(0x30,0x30,0x38,0xFF));
        }
        break;

    case VEHICLE_MAT_TEXTURED:
        set_vehicle_uv(uv);
        C3D_TexBind(0, (C3D_Tex *)tex);
        if (vehicle->psp_texture_alpha_is_mask) {
            if (vehicle_asset_psp_texture_uses_alpha(vehicle, group->material))
                bind_vehicle_texenv_decal();
            else
                bind_vehicle_texenv_texture_opaque();
        } else {
            bind_vehicle_texenv_texture();
        }
        break;

    case VEHICLE_MAT_GOURAUD:
        set_vehicle_uv(NULL);
        bind_vehicle_texenv_vertex();
        break;

    case VEHICLE_MAT_SPECIAL_DYNAMIC:
        /* 9999 is not globally "crew tag": Pursuit *2 bodies use it heavily.
           Until the original GAME::Material lookup is decoded, render a
           neutral deterministic surface instead of binding unrelated PNGs. */
        set_vehicle_uv(NULL);
        bind_vehicle_texenv_tint(gpu_rgba8(0x78,0x78,0x78,0xFF));
        break;

    case VEHICLE_MAT_UNTEXTURED:
    default:
        set_vehicle_uv(NULL);
        switch (group->material) {
        case 1002: bind_vehicle_texenv_tint(gpu_rgba8(0xA8,0xA8,0xA8,0xFF)); break; /* DRIVER */
        case 1003: bind_vehicle_texenv_tint(gpu_rgba8(0x20,0x20,0x20,0xFF)); break; /* MESH */
        case 1004: bind_vehicle_texenv_tint(gpu_rgba8(0x70,0x20,0x20,0xFF)); break; /* legend carbon */
        default:   bind_vehicle_texenv_vertex(); break;
        }
        break;
    }
}

static void set_vehicle_pass_state(int transparent)
{
    /* Never inherit blend/depth/cull state from PocketGarage, UI or another
       material. Thin customization pieces are rendered two-sided until the
       original CompiledModel cull flags are understood. */
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    if (transparent) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    } else {
        /* True opaque pass: source alpha must not blend BODY/BASE/HOOD/SPOILER
           with the garage behind them. */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_ONE, GPU_ZERO,
                       GPU_ONE, GPU_ZERO);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    }
}

static void set_vehicle_group_raster_state(VehicleMaterialClass cls, int transparent, int psp_mask_alpha)
{
    if (transparent)
        return;

    /* TextureShiny assets from the Zeebo build are RGBA decals even though
       they live in the opaque geometry pass.  In particular taillight and
       badge textures use 4-bit alpha extensively.  ONE/ZERO discarded that
       coverage and made the low-alpha RGB blocks visible as coloured noise.
       Keep depth writes (the geometry is still an opaque car surface), but
       composite the decal colour using its authored alpha. */
    if (cls == VEHICLE_MAT_TEXTURED && !psp_mask_alpha) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE, GPU_ZERO);
        C3D_AlphaTest(true, GPU_GREATER, 4);
    } else {
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
}

static void draw_vehicle_pass(const VehicleAsset *vehicle, int transparent)
{
    if (!vehicle)
        return;

    set_vehicle_pass_state(transparent);

    /* PICA200-safe vehicle compositor.
       Do not rebind the attribute buffer between BODY/BASE/customization parts.
       The previous implementations did that and hardware testing showed that
       the large BODY stream could then consume stale state (exploded vertices),
       while later HOOD/SPOILER draws could disappear.  Build one contiguous
       linear stream for the complete selected car and bind it exactly once. */
    const VehiclePartSlot draw_slots[] = {
        VEHICLE_SLOT_BODY,
        VEHICLE_SLOT_BASE,
        VEHICLE_SLOT_HOOD,
        VEHICLE_SLOT_SPOILER,
        VEHICLE_SLOT_CREWTAG_SIDES,
        VEHICLE_SLOT_CREWTAG_HOOD,
    };
    enum { SLOT_N = sizeof(draw_slots)/sizeof(draw_slots[0]) };
    uint32_t base[SLOT_N];
    size_t total = 0;
    for (unsigned s = 0; s < SLOT_N; ++s) {
        const N3PPart *part = vehicle_asset_part(vehicle, draw_slots[s]);
        base[s] = (uint32_t)total;
        if (part)
            total += part->draw_vertex_count;
    }
    if (!total)
        return;

    /* The selected vehicle is only a few thousand vertices in practice.  A
       frame-local linear copy is deliberate here: it makes the GPU ownership
       unambiguous and avoids retaining pointers to customization meshes that
       can be replaced by the garage UI. */
    if (!transparent) {
        if (total > s_vehicle_stream_capacity) {
            /* Old storage may still belong to the preceding submitted frame. */
            C3D_FrameSync();
            linearFree(s_vehicle_stream);
            s_vehicle_stream = linearAlloc(total * sizeof(*s_vehicle_stream));
            if (!s_vehicle_stream) {
                s_vehicle_stream_capacity = 0;
                return;
            }
            s_vehicle_stream_capacity = total;
        }
        for (unsigned s = 0; s < SLOT_N; ++s) {
            const N3PPart *part = vehicle_asset_part(vehicle, draw_slots[s]);
            s_vehicle_stream_base[draw_slots[s]] = base[s];
            if (part)
                memcpy(s_vehicle_stream + base[s], part->draw_verts,
                       (size_t)part->draw_vertex_count * sizeof(*s_vehicle_stream));
        }
        GSPGPU_FlushDataCache(s_vehicle_stream, total * sizeof(*s_vehicle_stream));
    }
    if (!s_vehicle_stream || total > s_vehicle_stream_capacity)
        return;

    C3D_BufInfo *buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, s_vehicle_stream, sizeof(N3PVertex), 3, 0x210);

    for (unsigned s = 0; s < SLOT_N; ++s) {
        const N3PPart *part = vehicle_asset_part(vehicle, draw_slots[s]);
        if (!part)
            continue;
        for (uint32_t g = 0; g < part->group_count; ++g) {
            const N3PGroup *group = &part->groups[g];
            VehicleMaterialClass cls = vehicle_material_classify(
                group->renderer, group->material,
                vehicle_asset_texture(vehicle, group->material) != NULL);
            int window = cls == VEHICLE_MAT_WINDOW;
            if (window != transparent)
                continue;

            /* With a single vertex stream there is no special replacement
               depth mode.  Restore the known-good reverse-Z state for every
               group so material binding cannot leak a previous pass state. */
            set_vehicle_group_raster_state(cls, transparent,
                                           vehicle->psp_texture_alpha_is_mask
                                           && !vehicle_asset_psp_texture_uses_alpha(vehicle, group->material));
            bind_vehicle_material(vehicle, draw_slots[s], group);
            C3D_DrawArrays(GPU_TRIANGLES,
                           (int)(base[s] + group->index_start),
                           (int)group->index_count);
        }
    }

    if (!transparent) {
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    }
}

/* TEV clearcoat: an alpha-composited specular-only pass on PAINT surfaces. */
static void draw_vehicle_clearcoat_pass(const VehicleAsset *vehicle)
{
    if (!vehicle || !s_vehicle_stream) return;
    static const VehiclePartSlot slots[] = {
        VEHICLE_SLOT_BODY, VEHICLE_SLOT_BASE, VEHICLE_SLOT_HOOD,
        VEHICLE_SLOT_SPOILER, VEHICLE_SLOT_CREWTAG_SIDES, VEHICLE_SLOT_CREWTAG_HOOD,
    };
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mat, &s_clearcoat_material);
    set_vehicle_uv(NULL);
    /* RGB is TEV-modulated; alpha is explicitly constant so the reflection
       is not lost when the specular-only vertex shader has zero alpha. */
    bind_vehicle_texenv_tint(gpu_rgba8(0xB8,0xD8,0xF0,0x4C));
    C3D_TexEnv *coat_env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(coat_env,C3D_Alpha,GPU_CONSTANT,0,0);
    C3D_TexEnvFunc(coat_env,C3D_Alpha,GPU_REPLACE);
    for (unsigned slot=0; slot<sizeof(slots)/sizeof(slots[0]); ++slot) {
        const N3PPart *part=vehicle_asset_part(vehicle,slots[slot]);
        if (!part) continue;
        uint32_t base=s_vehicle_stream_base[slots[slot]];
        for (uint32_t g=0; g<part->group_count; ++g) {
            const N3PGroup *group=&part->groups[g];
            if (vehicle_material_classify(group->renderer,group->material,
                vehicle_asset_texture(vehicle,group->material)!=NULL)!=VEHICLE_MAT_PAINT) continue;
            C3D_DrawArrays(GPU_TRIANGLES,(int)(base+group->index_start),(int)group->index_count);
        }
    }
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
}

static void draw_vehicle_wheels(const VehicleAsset *vehicle, const C3D_Mtx *view,
                                const C3D_Mtx *car_model, float rotation,
                                float steer)
{
    const C3D_Tex *wheel_tex = vehicle_asset_wheel_texture(vehicle);
    const TextureUV *wheel_uv = vehicle_asset_wheel_uv(vehicle);
    const C3D_Tex *wheel_tire_back = vehicle_asset_texture(vehicle, 1995);
    const TextureUV *wheel_tire_back_uv = vehicle_asset_texture_uv(vehicle, 1995);
    const C3D_Tex *wheel_caliper = vehicle_asset_texture(vehicle, 1996);
    const TextureUV *wheel_caliper_uv = vehicle_asset_texture_uv(vehicle, 1996);
    if (!wheel_tex || !wheel_uv || !s_wheel_stream || !s_wheel_tire_mesh)
        return;
    /* Scene batches change shader/attribute and blend state.  Wheels use a
       separate procedural stream, so restore the complete vehicle state. */
    bind_vehicle_program();
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    float front, rear, track, radius, center_y;
    vehicle_asset_wheel_placement(vehicle, &front, &rear, &track, &radius, &center_y);

    /* The fitted body envelope reaches the exterior sheet metal, whereas the
       original four wheel records store hub centres. Inset the approximation
       by tyre half-width so the wheel remains inside its arch. */
    float front_radius, rear_radius, front_y, rear_y;
    vehicle_asset_wheel_axles(vehicle, &front_radius, &rear_radius, &front_y, &rear_y);
    front_radius *= 0.78f;
    rear_radius *= 0.78f;
    float max_radius = front_radius > rear_radius ? front_radius : rear_radius;
    track -= max_radius * 0.55f;
    if (track < max_radius * 0.90f)
        track = max_radius * 0.90f;
    front_y -= front_radius * 0.03f;
    rear_y -= rear_radius * 0.03f;
    const float radii[4] = {front_radius, front_radius, rear_radius, rear_radius};
    const float ys[4] = {front_y, front_y, rear_y, rear_y};
    const float xs[4] = {front, front, rear, rear};
    const float zs[4] = {track, -track, track, -track};
    C3D_BufInfo *buf = C3D_GetBufInfo();
    for (int i = 0; i < 4; ++i) {
        C3D_Mtx model = *car_model, mv;
        const float wheel_radius = radii[i];
        Mtx_Translate(&model, xs[i], ys[i], zs[i], true);
        if (i < 2)
            Mtx_RotateY(&model, -steer * 0.48f, true);
        Mtx_RotateZ(&model, rotation, true);
        Mtx_Multiply(&mv, view, &model);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mv, &mv);

        /* The PSP mesh uses an authored local radius of about .316 and
           includes tyre, rim and brake surfaces in the same UV atlas. */
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE, GPU_ZERO);
        Mtx_Scale(&model, wheel_radius / 0.316f, wheel_radius / 0.316f,
                  wheel_radius / 0.316f);
        /* WHEEL_STOCK is a single asymmetric PSP wheel: its backing plate,
           tyre wall, brake and rim all occupy local -Z.  The vehicle uses
           +/-Z for the two sides, so the negative-Z instances must be
           mirrored along the hub axis.  Without this, the backing plate is
           exposed in front of the rim on one side of the car. */
        if (zs[i] < 0.0f)
            Mtx_Scale(&model, 1.0f, 1.0f, -1.0f);
        Mtx_Multiply(&mv, view, &model);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mv, &mv);
        BufInfo_Init(buf);
        BufInfo_Add(buf, s_wheel_stream, sizeof(N3PVertex), 3, 0x210);
        /* PSP wheel packets use three materials. Only 1990 samples the
           selected rim atlas; applying it to tyre and caliper caused the
           noisy, broken wheel pattern seen in the garage. */
        for (uint32_t group_i = 0; group_i < s_wheel_tire_mesh->group_count; ++group_i) {
            const N3PGroup *group = &s_wheel_tire_mesh->groups[group_i];
            /* Flat packets use their original shared TIRE_BACK/CALIPER
               maps. Their UVs are intentionally unrelated to the rim atlas. */
            if (group->reserved == 1) {
                const C3D_Tex *flat_tex = group->material == 1995 ? wheel_tire_back : wheel_caliper;
                const TextureUV *flat_uv = group->material == 1995 ? wheel_tire_back_uv : wheel_caliper_uv;
                C3D_AlphaTest(true, GPU_GREATER, 4);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                               GPU_ONE, GPU_ZERO);
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
                if (flat_tex && flat_uv) {
                    set_vehicle_uv(flat_uv);
                    C3D_TexBind(0, (C3D_Tex *)flat_tex);
                    bind_vehicle_texenv_texture();
                } else {
                    set_vehicle_uv(NULL);
                    bind_vehicle_texenv_tint(gpu_rgba8(0x20, 0x20, 0x24, 0xFF));
                }
            } else if (group->reserved == 2) { /* Chrome/rim atlas */
                C3D_AlphaTest(true, GPU_GREATER, 4);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                               GPU_ONE, GPU_ZERO);
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
                set_vehicle_uv(wheel_uv);
                C3D_TexBind(0, (C3D_Tex *)wheel_tex);
                bind_vehicle_texenv_texture();
            } else { /* Rubber shares PSP material 1990's wheel atlas. */
                C3D_AlphaTest(false, GPU_ALWAYS, 0);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                               GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
                set_vehicle_uv(wheel_uv);
                C3D_TexBind(0, (C3D_Tex *)wheel_tex);
                /* Rubber must not inherit the transparent spoke cut-out. */
                bind_vehicle_texenv_texture_opaque();
            }
            C3D_DrawArrays(GPU_TRIANGLES, s_wheel_tire_offset + group->index_start,
                           group->index_count);
        }
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
    }
}

static float batch_depth(const SceneBatch *b, const Camera *cam)
{
    float z=0;
    for (unsigned k=0;k<3;k++) z+=(b->center[k]-cam->eye[k])*(cam->at[k]-cam->eye[k]);
    return z;
}

static void draw_scene(SceneAsset *a, const C3D_Mtx *view, const Camera *cam, int transparent)
{
    if (!a || !a->section) return;
    SceneSection *s=a->section;
    C3D_BindProgram(&s_scene_program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_scene_proj,&s_proj);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_scene_mv,view);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_scene_tex_flip,
                  !strncmp(s->section, "opwd_3000/", 10) ? 1.0f : 0.0f,
                  0.0f, 0.0f, 0.0f);
    C3D_AttrInfo *attr=C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);
    AttrInfo_AddLoader(attr,1,GPU_FLOAT,2);
    AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);
    C3D_BufInfo *buf=C3D_GetBufInfo();
    BufInfo_Init(buf);BufInfo_Add(buf,s->vertices,sizeof(SceneVertex),3,0x210);
    C3D_DepthTest(true,GPU_GREATER,transparent?GPU_WRITE_COLOR:GPU_WRITE_ALL);
    unsigned count=0;
    for (uint32_t i=0;i<s->batch_count;i++) {
        unsigned mode=s->materials[s->batches[i].material].alpha_mode;
        if ((mode==2 || mode==3)!=transparent) continue;
        unsigned j=count++;
        while (transparent && j && batch_depth(&s->batches[a->order[j-1]],cam)<batch_depth(&s->batches[i],cam)) {
            a->order[j]=a->order[j-1];j--;
        }
        a->order[j]=i;
    }
    for (unsigned i=0;i<count;i++) {
        const SceneBatch *b=&s->batches[a->order[i]];
        unsigned mode=s->materials[b->material].alpha_mode;
        if (mode == 3)
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                           GPU_SRC_ALPHA, GPU_ONE,
                           GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
        else
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                           GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                           GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        C3D_AlphaTest(mode==1,GPU_GREATER,127);
        C3D_TexBind(0,a->textures[b->material]);bind_texenv_texture();
        C3D_DrawElements(GPU_TRIANGLES,b->count,C3D_UNSIGNED_SHORT,s->indices+b->first);
    }
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
}

static int streamed_section_visible(const WorldStreamSection *s, const Camera *cam)
{
    /* Coarse X/Z cone test before issuing any PICA commands.  The cell remains
       resident for collision; this only avoids drawing sections behind the car. */
    float x=(s->min_x+s->max_x)*0.5f-cam->eye[0];
    float z=(s->min_z+s->max_z)*0.5f-cam->eye[2];
    float radius=fmaxf(s->max_x-s->min_x,s->max_z-s->min_z)*0.5f;
    /* The visible circle is intentionally smaller than the streaming cache. */
    if (x*x+z*z > (230.0f+radius)*(230.0f+radius)) return 0;
    float fx=cam->at[0]-cam->eye[0], fz=cam->at[2]-cam->eye[2];
    float fl=sqrtf(fx*fx+fz*fz); if(fl<0.001f) return 1;
    float forward=(x*fx+z*fz)/fl;
    float side=fabsf(x*fz-z*fx)/fl;
    return forward+radius>-30.0f && side<forward*1.25f+radius+45.0f;
}

static void draw_streamed_world(const WorldStream *world, const C3D_Mtx *view,
                                const Camera *cam)
{
    if (!world) return;
    for (unsigned i=0; i<world->count; ++i) {
        const WorldStreamSection *s=&world->sections[i];
        if (!s->loaded || !streamed_section_visible(s,cam)) continue;
        C3D_Mtx model, section_view;
        Mtx_Identity(&model);
        Mtx_Translate(&model,s->x,s->y,s->z,true);
        Mtx_Multiply(&section_view,view,&model);
        draw_scene((SceneAsset *)&s->asset,&section_view,cam,0);
        draw_scene((SceneAsset *)&s->asset,&section_view,cam,1);
    }
}

static void bind_vehicle_program(void)
{
    C3D_BindProgram(&s_program);
    /* Shader constants share GPU registers: restore every vehicle uniform. */
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_proj, &s_proj);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mat, &s_material);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_light, 0.35f, -0.65f, -0.55f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_half,  0.25f, -0.50f, -0.70f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_u_lclr,  1.0f, 0.88f, 0.70f, 1.0f);
    set_vehicle_uv(NULL);
    C3D_AttrInfo *attr=C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);
    AttrInfo_AddLoader(attr,1,GPU_FLOAT,2);
    AttrInfo_AddLoader(attr,2,GPU_FLOAT,3);
}

static void effect_quad(N3PVertex *out, float x, float y, float z,
                        float hy, float hz, float nx)
{
    const N3PVertex q[6] = {
        {{x,y-hy,z-hz},{0,1},{nx,0,0}}, {{x,y+hy,z-hz},{0,0},{nx,0,0}},
        {{x,y+hy,z+hz},{1,0},{nx,0,0}}, {{x,y-hy,z-hz},{0,1},{nx,0,0}},
        {{x,y+hy,z+hz},{1,0},{nx,0,0}}, {{x,y-hy,z+hz},{1,1},{nx,0,0}},
    };
    memcpy(out,q,sizeof(q));
}

/* Camera-facing lamp mesh.  The centre is transformed by the car first;
 * only the lens plane turns toward the player, like classic N64 foliage. */
#define LENS_SIDES 8
#define LENS_VERTS (LENS_SIDES * 3)
static void effect_billboard(N3PVertex *out, C3D_FVec centre, float hy, float hx)
{
    for (int i=0;i<LENS_SIDES;++i) {
        float a0=(float)i*(2.0f*(float)M_PI/(float)LENS_SIDES);
        float a1=(float)(i+1)*(2.0f*(float)M_PI/(float)LENS_SIDES);
        float y0=cosf(a0), x0=sinf(a0), y1=cosf(a1), x1=sinf(a1);
        N3PVertex *t=out+i*3;
        t[0]=(N3PVertex){{centre.x,centre.y,centre.z},{.5f,.5f},{0,0,-1}};
        t[1]=(N3PVertex){{centre.x+x0*hx,centre.y+y0*hy,centre.z},
                          {.5f+.5f*x0,.5f-.5f*y0},{0,0,-1}};
        t[2]=(N3PVertex){{centre.x+x1*hx,centre.y+y1*hy,centre.z},
                          {.5f+.5f*x1,.5f-.5f*y1},{0,0,-1}};
    }
}

static void draw_vehicle_effects(const VehicleAsset *vehicle, const C3D_Mtx *view,
                                 const C3D_Mtx *car_model, int race_active, int braking, float wheelspin)
{
    if (!vehicle || !s_effect_stream) return;
    s_effect_time += .032f;
    if (s_effect_time > 1000.0f) s_effect_time -= 1000.0f;
    float length=vehicle->bounds_max[0]-vehicle->bounds_min[0];
    float width=vehicle->bounds_max[2]-vehicle->bounds_min[2];
    float height=vehicle->bounds_max[1]-vehicle->bounds_min[1];
    if (length < .5f || width < .3f || height < .2f) return;
    float front,rear,front_y,rear_y,front_z,rear_z,front_h,front_w,rear_h,rear_w;
    if (vehicle->light_fit_valid) {
        front=vehicle->light_front_x; front_y=vehicle->light_front_y; front_z=vehicle->light_front_z;
        front_h=vehicle->light_front_h; front_w=vehicle->light_front_w;
        rear=vehicle->light_rear_x; rear_y=vehicle->light_rear_y; rear_z=vehicle->light_rear_z;
        rear_h=vehicle->light_rear_h; rear_w=vehicle->light_rear_w;
    } else {
        front=vehicle->bounds_max[0]-length*.035f; rear=vehicle->bounds_min[0]+length*.035f;
        front_y=rear_y=vehicle->bounds_min[1]+height*.42f;
        front_z=rear_z=width*.34f;
        front_h=rear_h=height*.070f; front_w=rear_w=width*.085f;
    }
    /* Slight overscan allows the radial alpha to fade into the body. */
    front_h*=1.30f; front_w*=1.30f; rear_h*=1.24f; rear_w*=1.24f;
    C3D_Mtx car_mv; Mtx_Multiply(&car_mv,view,car_model);
    effect_billboard(s_effect_stream+0,
        Mtx_MultiplyFVecH(&car_mv,FVec3_New(front,front_y,front_z)),front_h,front_w);
    effect_billboard(s_effect_stream+LENS_VERTS,
        Mtx_MultiplyFVecH(&car_mv,FVec3_New(front,front_y,-front_z)),front_h,front_w);
    effect_billboard(s_effect_stream+LENS_VERTS*2,
        Mtx_MultiplyFVecH(&car_mv,FVec3_New(rear,rear_y,rear_z)),rear_h,rear_w);
    effect_billboard(s_effect_stream+LENS_VERTS*3,
        Mtx_MultiplyFVecH(&car_mv,FVec3_New(rear,rear_y,-rear_z)),rear_h,rear_w);
    GSPGPU_FlushDataCache(s_effect_stream,sizeof(N3PVertex)*LENS_VERTS*4);
    bind_vehicle_program();
    C3D_Mtx mv; Mtx_Identity(&mv);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_mv,&mv);
    C3D_BufInfo *buf=C3D_GetBufInfo();
    BufInfo_Init(buf); BufInfo_Add(buf,s_effect_stream,sizeof(N3PVertex),3,0x210);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE,
                   GPU_SRC_ALPHA,GPU_ONE);
    set_vehicle_uv(&s_effect_texture_uv[0]);
    C3D_TexBind(0,&s_effect_textures[0]); bind_vehicle_texenv_texture();
    C3D_DrawArrays(GPU_TRIANGLES,0,LENS_VERTS*2);
    /* Tail bloom is deliberately translucent, like the Zeebo lens overlay.
       Only the braking core receives a second additive pass. */
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);
    set_vehicle_uv(&s_effect_texture_uv[1]);
    C3D_TexBind(0,&s_effect_textures[1]); bind_vehicle_texenv_texture();
    C3D_DrawArrays(GPU_TRIANGLES,LENS_VERTS*2,LENS_VERTS*2);
    if (braking) {
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE,
                       GPU_SRC_ALPHA,GPU_ONE);
        C3D_DrawArrays(GPU_TRIANGLES,LENS_VERTS*2,LENS_VERTS*2);
    }

    if (race_active && wheelspin > .20f) {
        /* Three staggered puffs expand and rise from the rear tyres.  Their
           different phases make the smoke dissipate rather than flicker. */
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_mv,&car_mv);
        for (int i=0;i<3;++i) {
            float phase=fmodf(s_effect_time*.72f+(float)i/3.0f,1.0f);
            float puff=(.08f+.24f*phase)*(.55f+.45f*wheelspin);
            float side=(i&1)?-1.0f:1.0f;
            effect_quad(s_effect_stream+i*6,
                        rear-.16f-phase*(.28f+wheelspin*.55f),
                        vehicle->bounds_min[1]+height*.12f+phase*.11f,
                        side*rear_z*(1.02f+phase*.12f),puff,puff,.0f);
        }
        GSPGPU_FlushDataCache(s_effect_stream,sizeof(N3PVertex)*18);
        BufInfo_Init(buf); BufInfo_Add(buf,s_effect_stream,sizeof(N3PVertex),3,0x210);
        set_vehicle_uv(&s_effect_texture_uv[2]);
        C3D_TexBind(0,&s_effect_textures[2]); bind_vehicle_texenv_texture();
        C3D_DrawArrays(GPU_TRIANGLES,0,18);
    }
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
}

void renderer_draw_world(const Camera *cam, const VehicleAsset *vehicle,
                         float yaw, float x, float y, float z,
                         float wheel_rotation, float steer_visual,
                         float body_roll, float body_pitch,
                         int brake_lights, float wheelspin,
                         SceneAsset *scene, const WorldStream *world, int test_world,
                         int defer_vehicle_windows)
{
    Mtx_PerspTilt(&s_proj, C3D_AngleFromDegrees(56.0f),
                  C3D_AspectRatioTop, 0.15f, 230.0f, false);

    C3D_FVec eye = FVec3_New(cam->eye[0], cam->eye[1], cam->eye[2]);
    C3D_FVec at  = FVec3_New(cam->at[0], cam->at[1], cam->at[2]);
    C3D_FVec up  = FVec3_New(0.0f, 1.0f, 0.0f);
    C3D_Mtx view, model, wheel_model, mv;
    Mtx_LookAt(&view, eye, at, up, false);

    draw_scene(scene, &view, cam, 0);
    draw_streamed_world(world, &view, cam);

    /* Draw the horizon after opaque city geometry, but only into pixels that
       still hold the cleared reverse-Z value.  The old pre-world pass was
       valid yet got hidden by large opaque scene batches. */
    /* Sky composition is disabled until it can share the world depth path
       without contaminating the HUD blend state. */

    bind_vehicle_program();


    if (test_world && !world) {
        /* Temporary test ground. This will be replaced by streamed CDL sections. */
        Mtx_Identity(&model);
        Mtx_Multiply(&mv, &view, &model);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mv, &mv);
        bind_texenv_vertex();

        C3D_BufInfo *buf = C3D_GetBufInfo();
        BufInfo_Init(buf);
        BufInfo_Add(buf, s_ground, sizeof(N3PVertex), 3, 0x210);
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);

        BufInfo_Init(buf);
        BufInfo_Add(buf, s_road, sizeof(N3PVertex), 3, 0x210);
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
        const u32 colors[4] = {
            gpu_rgba8(0x30,0x30,0x30,0xFF),
            gpu_rgba8(0xDD,0xDD,0xDD,0xFF),
            gpu_rgba8(0x66,0x99,0xEE,0xFF),
            gpu_rgba8(0xEE,0xEE,0xEE,0xFF)
        };
        for (int band = 0; band < 4; ++band) {
            C3D_TexEnvColor(env, colors[band]);
            if (band < 3)
                C3D_DrawArrays(GPU_TRIANGLES, band*ROAD_SEGMENTS*6, ROAD_SEGMENTS*6);
            else
                for (int i = 0; i < ROAD_SEGMENTS; i += 2)
                    C3D_DrawArrays(GPU_TRIANGLES, band*ROAD_SEGMENTS*6+i*6, 6);
        }

    }

    if (!vehicle) { draw_scene(scene, &view, cam, 1); return; }

    /* Vehicle assets are +X length. Game forward is +Z. */
    /* Tyres remain on the contact plane.  Weight transfer bends only the
       sprung body, which keeps the wheel arches believable on slopes. */
    Mtx_Identity(&wheel_model);
    Mtx_Translate(&wheel_model, x, y, z, true);
    Mtx_RotateY(&wheel_model, yaw, true);
    Mtx_RotateY(&wheel_model, -M_PI / 2.0f, true);
    model = wheel_model;
    Mtx_RotateX(&model, body_roll, true);
    Mtx_RotateZ(&model, body_pitch, true);
    Mtx_Multiply(&mv, &view, &model);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mv, &mv);

    /* Opaque/shiny packets first, window packets second. */
    draw_vehicle_pass(vehicle, 0);
    draw_vehicle_clearcoat_pass(vehicle);
    draw_vehicle_wheels(vehicle, &view, &wheel_model, wheel_rotation, steer_visual);
    draw_vehicle_effects(vehicle, &view, &model, test_world, brake_lights, wheelspin);
    draw_scene(scene, &view, cam, 1);
    bind_vehicle_program();
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_u_mv, &mv);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    if (!defer_vehicle_windows)
        draw_vehicle_pass(vehicle, 1);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

    /* Render only the existing emissive vehicle lenses into a small VRAM
       texture, then filter it over the scene additively. This is a real
       post-lighting pass without rerendering the streamed city. */
    C3D_RenderTarget *glow_target=core_glow_target();
    C3D_Tex *glow_texture=core_glow_texture();
    if (glow_target && glow_texture && vehicle) {
        C3D_FrameDrawOn(glow_target);
        C3D_RenderTargetClear(glow_target, C3D_CLEAR_ALL, 0x00000000, 0);
        draw_vehicle_effects(vehicle, &view, &model, test_world, brake_lights, 0.0f);
        C3D_FrameDrawOn(core_top_target());
        C3D_Mtx glow_projection, glow_identity;
        begin_ui(&glow_projection, &glow_identity);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE, GPU_ONE, GPU_ONE);
        ui_quad(0,0,400,240,0xA0FFFFFF,glow_texture,&s_glow_uv);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    }
}

void renderer_draw_garage_preview(const Camera *cam, const VehicleAsset *vehicle,
                                  float yaw, float x, float y, float z, float scale,
                                  int preview_id)
{
    if (!cam || !vehicle || !vehicle_asset_part(vehicle,VEHICLE_SLOT_BODY)
        || preview_id < 0 || preview_id >= 4) return;
    N3PVertex *saved_stream=s_vehicle_stream;
    size_t saved_capacity=s_vehicle_stream_capacity;
    s_vehicle_stream=s_garage_stream[preview_id];
    s_vehicle_stream_capacity=s_garage_stream_capacity[preview_id];
    C3D_Mtx projection, view, model, mv;
    Mtx_PerspTilt(&projection,C3D_AngleFromDegrees(56.0f),C3D_AspectRatioTop,.15f,230.0f,false);
    C3D_FVec eye=FVec3_New(cam->eye[0],cam->eye[1],cam->eye[2]);
    C3D_FVec at=FVec3_New(cam->at[0],cam->at[1],cam->at[2]);
    Mtx_LookAt(&view,eye,at,FVec3_New(0,1,0),false);
    Mtx_Identity(&model);
    Mtx_Translate(&model,x,y,z,true);
    Mtx_RotateY(&model,yaw,true);
    Mtx_RotateY(&model,-M_PI/2.0f,true);
    Mtx_Scale(&model,scale,scale,scale);
    Mtx_Multiply(&mv,&view,&model);
    bind_vehicle_program();
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_proj,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_mv,&mv);
    draw_vehicle_pass(vehicle,0);
    draw_vehicle_wheels(vehicle,&view,&model,0.0f,0.0f);
    s_garage_stream[preview_id]=s_vehicle_stream;
    s_garage_stream_capacity[preview_id]=s_vehicle_stream_capacity;
    s_vehicle_stream=saved_stream;
    s_vehicle_stream_capacity=saved_capacity;
}

void renderer_draw_garage_windows(const Camera *cam, const VehicleAsset *vehicle,
                                  float yaw, float x, float y, float z,
                                  float body_roll, float body_pitch, float scale,
                                  int preview_id)
{
    if (!cam || !vehicle) return;
    N3PVertex *saved_stream=s_vehicle_stream;
    size_t saved_capacity=s_vehicle_stream_capacity;
    if (preview_id >= 0 && preview_id < 4) {
        s_vehicle_stream=s_garage_stream[preview_id];
        s_vehicle_stream_capacity=s_garage_stream_capacity[preview_id];
    }
    if (!s_vehicle_stream) { s_vehicle_stream=saved_stream; s_vehicle_stream_capacity=saved_capacity; return; }
    C3D_Mtx projection,view,model,mv;
    Mtx_PerspTilt(&projection,C3D_AngleFromDegrees(56.0f),C3D_AspectRatioTop,.15f,230.0f,false);
    Mtx_LookAt(&view,FVec3_New(cam->eye[0],cam->eye[1],cam->eye[2]),
               FVec3_New(cam->at[0],cam->at[1],cam->at[2]),FVec3_New(0,1,0),false);
    Mtx_Identity(&model);
    Mtx_Translate(&model,x,y,z,true);
    Mtx_RotateY(&model,yaw,true);
    Mtx_RotateY(&model,-M_PI/2.0f,true);
    Mtx_RotateX(&model,body_roll,true);
    Mtx_RotateZ(&model,body_pitch,true);
    Mtx_Scale(&model,scale,scale,scale);
    Mtx_Multiply(&mv,&view,&model);
    bind_vehicle_program();
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_proj,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_u_mv,&mv);
    draw_vehicle_pass(vehicle,1);
    if (preview_id >= 0 && preview_id < 4) {
        s_garage_stream[preview_id]=s_vehicle_stream;
        s_garage_stream_capacity[preview_id]=s_vehicle_stream_capacity;
    }
    s_vehicle_stream=saved_stream;
    s_vehicle_stream_capacity=saved_capacity;
}

void renderer_draw_hud_console(const char *screen_name, const char *camera_name,
                               float speed_kmh, float nitro, const char *hint)
{
    printf("\x1b[1;1H");
    printf("Need for Speed Carbon  (Zeebo -> 3DS)\n");
    printf("FE: %-16s\n", screen_name);
    printf("cam %s\n", camera_name);
    printf("SPD %6.1f km/h   N2O %3.0f%%\n", speed_kmh, nitro * 100.0f);
    printf("\n%s\n", hint);
    printf("A accel  B brake  R nitro  L look\n");
    printf("Y camera   SELECT back   L+R+SELECT exit\n");
}

static void ui_quad(float x, float y, float w, float h, u32 rgba,
                    C3D_Tex *texture, const TextureUV *uv)
{
    float r=((rgba>>24)&255)/255.0f, g=((rgba>>16)&255)/255.0f;
    float b=((rgba>>8)&255)/255.0f, a=(rgba&255)/255.0f;

    float tl_u=0.0f, tl_v=0.0f, tr_u=1.0f, tr_v=0.0f;
    float br_u=1.0f, br_v=1.0f, bl_u=0.0f, bl_v=1.0f;
    if (texture && uv) {
        /* T3X texture memory is vertically addressed from its lower edge.
           UI source images are top-left addressed, so flip V at the UI
           boundary instead of changing model/material UVs. */
        texture_uv_apply(uv, 0.0f, 1.0f, &tl_u, &tl_v);
        texture_uv_apply(uv, 1.0f, 1.0f, &tr_u, &tr_v);
        texture_uv_apply(uv, 1.0f, 0.0f, &br_u, &br_v);
        texture_uv_apply(uv, 0.0f, 0.0f, &bl_u, &bl_v);
    }

    const SceneVertex v[6] = {
        {{x,y,0},{tl_u,tl_v},{r,g,b,a}},
        {{x+w,y,0},{tr_u,tr_v},{r,g,b,a}},
        {{x+w,y+h,0},{br_u,br_v},{r,g,b,a}},
        {{x,y,0},{tl_u,tl_v},{r,g,b,a}},
        {{x+w,y+h,0},{br_u,br_v},{r,g,b,a}},
        {{x,y+h,0},{bl_u,bl_v},{r,g,b,a}},
    };
    SceneVertex *quad=s_ui_quad + (s_ui_quad_cursor++ % UI_QUAD_POOL)*6;
    memcpy(quad, v, sizeof(v));
    GSPGPU_FlushDataCache(quad, sizeof(v));
    C3D_BufInfo *buf=C3D_GetBufInfo();
    BufInfo_Init(buf);BufInfo_Add(buf,quad,sizeof(SceneVertex),3,0x210);
    if (texture) {
        C3D_TexBind(0, texture);
        bind_texenv_texture();
    } else {
        bind_texenv_vertex();
    }
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}


/* Draw a top-left-addressed rectangle from a texture atlas.  The public UI
   coordinates stay in source pixels, while TextureUV keeps tex3ds atlas
   rotation/padding invisible to HUD callers. */
static void ui_quad_region(float x, float y, float w, float h, u32 rgba,
                           C3D_Tex *texture, const TextureUV *uv,
                           float sx, float sy, float sw, float sh,
                           float source_w, float source_h)
{
    float r=((rgba>>24)&255)/255.0f, g=((rgba>>16)&255)/255.0f;
    float b=((rgba>>8)&255)/255.0f, a=(rgba&255)/255.0f;
    float u0=sx/source_w, u1=(sx+sw)/source_w;
    /* The logical crop keeps its source interval, but its V direction is
       reversed at the PICA UI boundary so atlas glyphs remain upright. */
    float vt=(sy+sh)/source_h, vb=sy/source_h;
    float tl_u,tl_v,tr_u,tr_v,br_u,br_v,bl_u,bl_v;
    texture_uv_apply(uv,u0,vt,&tl_u,&tl_v);
    texture_uv_apply(uv,u1,vt,&tr_u,&tr_v);
    texture_uv_apply(uv,u1,vb,&br_u,&br_v);
    texture_uv_apply(uv,u0,vb,&bl_u,&bl_v);
    const SceneVertex v[6] = {
        {{x,y,0},{tl_u,tl_v},{r,g,b,a}}, {{x+w,y,0},{tr_u,tr_v},{r,g,b,a}},
        {{x+w,y+h,0},{br_u,br_v},{r,g,b,a}}, {{x,y,0},{tl_u,tl_v},{r,g,b,a}},
        {{x+w,y+h,0},{br_u,br_v},{r,g,b,a}}, {{x,y+h,0},{bl_u,bl_v},{r,g,b,a}},
    };
    SceneVertex *quad=s_ui_quad + (s_ui_quad_cursor++ % UI_QUAD_POOL)*6;
    memcpy(quad,v,sizeof(v)); GSPGPU_FlushDataCache(quad,sizeof(v));
    C3D_BufInfo *buf=C3D_GetBufInfo(); BufInfo_Init(buf);
    BufInfo_Add(buf,quad,sizeof(SceneVertex),3,0x210);
    C3D_TexBind(0,texture); bind_texenv_texture(); C3D_DrawArrays(GPU_TRIANGLES,0,6);
}

static void ui_quad_rotated(float cx, float cy, float size, float angle, u32 rgba,
                            C3D_Tex *texture, const TextureUV *uv)
{
    float r=((rgba>>24)&255)/255.0f, g=((rgba>>16)&255)/255.0f;
    float b=((rgba>>8)&255)/255.0f, a=(rgba&255)/255.0f;
    float c=cosf(angle), sn=sinf(angle), h=size*.5f;
    float x[4]={-h,h,h,-h}, y[4]={-h,-h,h,h};
    float u[4]={0,1,1,0}, v[4]={1,1,0,0};
    if (uv) for (int i=0;i<4;i++) texture_uv_apply(uv,u[i],v[i],&u[i],&v[i]);
    SceneVertex q[6]; int idx[6]={0,1,2,0,2,3};
    for (int i=0;i<6;i++) { int n=idx[i];
        q[i]=(SceneVertex){{cx+x[n]*c-y[n]*sn,cy+x[n]*sn+y[n]*c,0},
                           {u[n],v[n]},{r,g,b,a}};
    }
    SceneVertex *quad=s_ui_quad + (s_ui_quad_cursor++ % UI_QUAD_POOL)*6;
    memcpy(quad,q,sizeof(q)); GSPGPU_FlushDataCache(quad,sizeof(q));
    C3D_BufInfo *buf=C3D_GetBufInfo(); BufInfo_Init(buf);
    BufInfo_Add(buf,quad,sizeof(SceneVertex),3,0x210);
    if (texture) { C3D_TexBind(0,texture); bind_texenv_texture(); }
    else bind_texenv_vertex();
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}


static void ui_quad_rotated_rect(float cx, float cy, float w, float h, float angle,
                                 u32 rgba, C3D_Tex *texture, const TextureUV *uv)
{
    float r=((rgba>>24)&255)/255.0f, g=((rgba>>16)&255)/255.0f;
    float b=((rgba>>8)&255)/255.0f, a=(rgba&255)/255.0f;
    float c=cosf(angle), sn=sinf(angle), hw=w*.5f, hh=h*.5f;
    float x[4]={-hw,hw,hw,-hw}, y[4]={-hh,-hh,hh,hh};
    float u[4]={0,1,1,0}, v[4]={1,1,0,0};
    for (int i=0;i<4;i++) texture_uv_apply(uv,u[i],v[i],&u[i],&v[i]);
    SceneVertex q[6]; const int idx[6]={0,1,2,0,2,3};
    for (int i=0;i<6;i++) { int n=idx[i];
        q[i]=(SceneVertex){{cx+x[n]*c-y[n]*sn,cy+x[n]*sn+y[n]*c,0},
                           {u[n],v[n]},{r,g,b,a}};
    }
    SceneVertex *quad=s_ui_quad + (s_ui_quad_cursor++ % UI_QUAD_POOL)*6;
    memcpy(quad,q,sizeof(q)); GSPGPU_FlushDataCache(quad,sizeof(q));
    C3D_BufInfo *buf=C3D_GetBufInfo(); BufInfo_Init(buf);
    BufInfo_Add(buf,quad,sizeof(SceneVertex),3,0x210);
    C3D_TexBind(0,texture); bind_texenv_texture(); C3D_DrawArrays(GPU_TRIANGLES,0,6);
}



static void ui_hud_glyph(float x, float y, float w, float h, int index,
                         C3D_Tex *texture, const TextureUV *uv,
                         float cell_w, float source_w)
{
    ui_quad_region(x,y,w,h,0xFFFFFFFF,texture,uv,(float)index*cell_w,
                   0,cell_w,15,source_w,15);
}

static void begin_ui(C3D_Mtx *projection, C3D_Mtx *identity)
{
    s_ui_quad_cursor=0;
    Mtx_OrthoTilt(projection,0.0f,400.0f,0.0f,240.0f,0.0f,1.0f,true);
    Mtx_Identity(identity);
    C3D_BindProgram(&s_scene_program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_scene_proj,projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,s_scene_mv,identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_scene_tex_flip, 0.0f, 0.0f, 0.0f, 0.0f);
    C3D_AttrInfo *attr=C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);
    AttrInfo_AddLoader(attr,1,GPU_FLOAT,2);
    AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,
                   GPU_ONE_MINUS_SRC_ALPHA,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);
}

static void draw_ui_text(const char *text, float x, float y, float scale, u32 color)
{
    C2D_Text parsed;
    C2D_TextParse(&parsed, s_text_buffer, text);
    C2D_TextOptimize(&parsed);
    C2D_DrawText(&parsed, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void renderer_draw_garage_hud(int category, const char *car_name, int fleet_mode, int active_slot,
                              unsigned cash, const unsigned char performance[5],
                              const char *const fleet_names[5], int visual_choice, int visual_count, int detail_open)
{
    if (!s_ui_quad || s_garage_icon_count != 5 || s_performance_icon_count != 5 || !s_text_buffer) return;
    C3D_Mtx projection, identity;
    begin_ui(&projection, &identity);
    static const char *const perf_labels[] = {"ENGINE", "TURBO", "CHASSIS", "HANDLING", "NITRO"};
    static const char *const visual_labels[] = {"BODY", "HOOD", "SPOILER", "PAINT", "RIMS"};
    int performance_tab = category >= 0 && category < 5;
    int local = performance_tab ? category : category - 5;
    if (local < 0 || local > 4) local=0;
    if (fleet_mode && s_garage_car_icon_loaded) {
        ui_quad(54,10,292,40,0x080B0ED8,NULL,NULL);
        static const int fleet_order[5]={3,1,0,2,4};
        for (int i=0;i<5;i++) {
            int slot=fleet_order[i];
            float x=76.0f+i*57.0f;
            ui_quad(x-4,12,48,34,slot==active_slot ? 0x21AEEAFF : 0x20262CB8,NULL,NULL);
            ui_quad(x+7,13,28,28,0xFFFFFFFF,&s_garage_car_icon,&s_garage_car_icon_uv);
        }
    } else {
        if (visual_count < 1) visual_count=1;
        if (visual_choice < 0) visual_choice=0;
        if (visual_choice >= visual_count) visual_choice=visual_count-1;
        ui_quad(20,14,360,28,0x071016E8,NULL,NULL);
        ui_quad(29,177,342,60,0x081018E8,NULL,NULL);
        if (!detail_open) {
            for (int i=0;i<5;i++) {
                float x=48.0f+i*61.0f;
                ui_quad(x-4,183,52,46,i==local ? 0xC75B18D8 : 0x20262CB8,NULL,NULL);
                if (performance_tab) ui_quad(x,181,42,42,0xFFFFFFFF,&s_performance_icons[i],&s_performance_icon_uv[i]);
                else ui_quad(x,181,42,42,0xFFFFFFFF,&s_garage_icons[i],&s_garage_icon_uv[i]);
            }
        } else if (performance_tab) {
            unsigned level=performance ? performance[local] : 0;
            for (int i=0;i<4;i++) {
                float x=78.0f+i*67.0f;
                u32 plate=(unsigned)i < level ? 0x2564D9E8 : ((unsigned)i == level ? 0xC75B18D8 : 0x20262CB8);
                ui_quad(x-4,183,56,46,plate,NULL,NULL);
                ui_quad(x+3,181,42,42,0xFFFFFFFF,&s_performance_icons[local],&s_performance_icon_uv[local]);
            }
        } else {
            int first=visual_choice-2; if (first < 0) first=0;
            if (first > visual_count-5) first=visual_count > 5 ? visual_count-5 : 0;
            for (int i=0;i<5 && first+i<visual_count;i++) {
                float x=48.0f+i*61.0f; int option=first+i;
                ui_quad(x-4,183,52,46,option==visual_choice ? 0xC75B18D8 : 0x20262CB8,NULL,NULL);
                ui_quad(x,181,42,42,option==visual_choice ? 0xFFFFFFFF : 0xB8D8E8FF,&s_garage_icons[local],&s_garage_icon_uv[local]);
            }
        }
    }
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C2D_SceneBegin(core_top_target());
    C2D_TextBufClear(s_text_buffer); C2D_Prepare();
    if (fleet_mode && s_garage_car_icon_loaded) {
        static const int fleet_order[5]={3,1,0,2,4};
        for (int i=0;i<5;i++) {
            const char *name=fleet_names && fleet_names[fleet_order[i]] ? fleet_names[fleet_order[i]] : "EMPTY";
            draw_ui_text(name,76.0f+i*57.0f,44,.20f,C2D_Color32(185,220,235,255));
        }
        draw_ui_text(car_name ? car_name : "CAR",116,58,.58f,C2D_Color32(255,255,255,255));
        draw_ui_text("A CUSTOMIZE   X DEALER   L REMOVE",82,74,.38f,C2D_Color32(115,220,255,255));
    } else {
        char cash_text[40]; snprintf(cash_text,sizeof(cash_text),"CASH %u",cash);
        draw_ui_text(performance_tab ? "PERFORMANCE" : "VISUAL",30,20,.52f,C2D_Color32(104,230,255,255));
        draw_ui_text(car_name ? car_name : "CAR",213,20,.50f,C2D_Color32(255,255,255,255));
        draw_ui_text(cash_text,300,20,.42f,C2D_Color32(164,255,184,255));
        if (!detail_open) {
            draw_ui_text(performance_tab ? "SELECT PERFORMANCE CATEGORY" : "SELECT VISUAL CATEGORY",30,145,.56f,C2D_Color32(255,255,255,255));
            draw_ui_text(performance_tab ? perf_labels[local] : visual_labels[local],30,163,.42f,C2D_Color32(119,220,255,255));
            draw_ui_text("A OPEN CATEGORY",30,178,.38f,C2D_Color32(119,220,255,255));
        } else if (performance_tab) {
            char level_text[64]; unsigned level=performance ? performance[local] : 0;
            snprintf(level_text,sizeof(level_text),"%s  LEVEL %u/4",perf_labels[local],level);
            draw_ui_text(level_text,30,142,.68f,C2D_Color32(255,255,255,255));
            draw_ui_text(level < 4 ? "A INSTALL NEXT LEVEL" : "MAXIMUM LEVEL",30,160,.40f,C2D_Color32(119,220,255,255));
            for (int i=0;i<4;i++) { char numeral[8]; snprintf(numeral,sizeof(numeral),"%d",i+1);
                draw_ui_text(numeral,96.0f+i*67.0f,218,.34f,C2D_Color32(220,240,250,255)); }
        } else {
            char part_text[64];
            snprintf(part_text,sizeof(part_text),"%s  %02d/%02d",visual_labels[local],visual_choice+1,visual_count);
            draw_ui_text(part_text,30,145,.68f,C2D_Color32(255,255,255,255));
            draw_ui_text("LEFT/RIGHT PREVIEW     A APPLY     START DRIVE",30,163,.36f,C2D_Color32(119,220,255,255));
            int first=visual_choice-2; if (first < 0) first=0;
            if (first > visual_count-5) first=visual_count > 5 ? visual_count-5 : 0;
            for (int i=0;i<5 && first+i<visual_count;i++) {
                char number[12]; snprintf(number,sizeof(number),"%02d",first+i+1);
                draw_ui_text(number,64.0f+i*61.0f,218,.30f,C2D_Color32(220,240,250,255));
            }
        }
        draw_ui_text(detail_open ? "X BACK TO CATEGORIES     START DRIVE" : "UP/DOWN CATEGORY     A OPEN     X RETURN",30,226,.34f,C2D_Color32(160,180,195,255));
    }
    C2D_Flush();
}

void renderer_draw_dealer_hud(const char *car_name, int index, int count, unsigned price, unsigned cash)
{
    if (!s_ui_quad || !s_text_buffer) return;
    C3D_Mtx projection, identity; begin_ui(&projection,&identity);
    ui_quad(18,14,364,36,0x071016E8,NULL,NULL);
    ui_quad(28,196,344,30,0x20262CB8,NULL,NULL);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C2D_SceneBegin(core_top_target()); C2D_TextBufClear(s_text_buffer); C2D_Prepare();
    char text[72];
    draw_ui_text("CAR DEALER",30,22,.66f,C2D_Color32(105,230,255,255));
    snprintf(text,sizeof(text),"%s",car_name ? car_name : "CAR"); draw_ui_text(text,42,154,.82f,C2D_Color32(255,255,255,255));
    snprintf(text,sizeof(text),"%d / %d",index+1,count); draw_ui_text(text,42,176,.45f,C2D_Color32(140,180,200,255));
    snprintf(text,sizeof(text),"PRICE %u",price); draw_ui_text(text,240,154,.58f,C2D_Color32(255,212,98,255));
    snprintf(text,sizeof(text),"CASH %u",cash); draw_ui_text(text,240,176,.45f,C2D_Color32(164,255,184,255));
    draw_ui_text("LEFT/RIGHT BROWSE     A BUY     X RETURN",46,204,.40f,C2D_Color32(115,220,255,255));
    C2D_Flush();
}

void renderer_draw_garage_transition(float progress, int to_customize)
{
    (void)to_customize;
    if (!s_ui_quad) return;
    /* One expanding quad is intentional. UI geometry shares a small dynamic
       buffer on 3DS, so two independently submitted shutter halves can alias
       and leave one half of the frame uncovered. */
    float cover;
    if (progress < .34f) cover = progress / .34f;
    else if (progress < .66f) cover = 1.0f;
    else cover = (1.0f-progress) / .34f;
    cover = fminf(1.0f, fmaxf(0.0f, cover));
    float h = 240.0f * cover;
    C3D_Mtx projection, identity;
    begin_ui(&projection, &identity);
    if (cover < .985f) {
        ui_quad(0,120.0f-h*.5f,400,h,0x000000FF,NULL,NULL);
    } else {
        /* The actual recovered QuickLoading background fills the blackout;
           the wheel is rotated as geometry rather than streamed per frame. */
        ui_quad(0,0,400,240,0xFFFFFFFF,&s_frontend_textures[2],&s_frontend_texture_uv[2]);
        ui_quad(0,164,400,76,0x000000C8,NULL,NULL);
        ui_quad_rotated(362,38,34,progress*18.849556f,0xFFFFFFFF,
                        &s_frontend_textures[3],&s_frontend_texture_uv[3]);
    }
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
}

void renderer_draw_race_hud(const Vehicle *vehicle, const RoadNetwork *roads)
{
    if (!vehicle || !s_ui_quad || !s_text_buffer) return;
    C3D_Mtx projection, identity;
    begin_ui(&projection, &identity);
    /* Match the Zeebo presentation: a clean race strip above, compact map
       left and circular tach/speed cluster right, leaving the road visible. */
    ui_quad(118,10,164,18,0x090B0DB8,NULL,NULL);
    ui_quad(121,12,158,2,0x43E7A8D8,NULL,NULL);
    /* The circular bezel is the only map background.  The actual streets are
       drawn from roadnetwork.bin below, so no rectangular full-map texture can
       leak outside the original round HUD mask. */
    ui_quad(12,14,72,72,0xFFFFFFFF,&s_hud_textures[7],&s_hud_texture_uv[7]);
    /* User-separated original layers, each kept at its authored dimensions. */
    const float tach_x=HUD_X, tach_y=240.0f-HUD_Y-HUD_H;
    const float tach_cx=tach_x+HUD_PIVOT_X, tach_cy=tach_y+HUD_H-HUD_PIVOT_Y;
    ui_quad(tach_x,tach_y,86,96,0xD8FFFFFF,&s_hud_textures[0],&s_hud_texture_uv[0]);
    if (vehicle->engine_rpm > 6900.0f && (((int)(s_effect_time*12.0f)) & 1))
        ui_quad(tach_x,tach_y+34,88,62,0xD0FFFFFF,&s_hud_textures[1],&s_hud_texture_uv[1]);
    float rpm=fmaxf(0.0f,fminf(1.0f,(vehicle->engine_rpm-900.0f)/6900.0f));
    float needle_angle=HUD_NEEDLE_START-rpm*HUD_NEEDLE_SWEEP;
    /* The left tip is the hub end: offset the rectangle so that tip stays
       locked at the dial centre while the authored needle rotates outward. */
    ui_quad_rotated_rect(tach_cx+cosf(needle_angle)*21.0f,
                         tach_cy+sinf(needle_angle)*21.0f,42,11,needle_angle,
                         0xFFFFFFFF,&s_hud_textures[2],&s_hud_texture_uv[2]);
    float nitro_fill=fmaxf(0.0f,fminf(1.0f,vehicle->nitro));
    float nitro_alpha=vehicle->nitro_cooldown > 0.0f ? 0.0f : 1.0f;
    if (nitro_alpha > 0.0f) {
        /* The original HUD exposes only the active green arc. */
        if (nitro_fill > .005f)
            ui_quad_region(tach_x+HUD_BAR_X,tach_y+HUD_H-HUD_BAR_Y-HUD_BAR_H,
                           HUD_BAR_W*nitro_fill,HUD_BAR_H,
                           0xFFFFFFFF,&s_hud_textures[5],&s_hud_texture_uv[5],
                           0,0,60.0f*nitro_fill,33,60,33);
        /* Keep the icon distinct from the curved fill: it occupies the
           inner lower-left field and is submitted after the bright bar. */
        ui_quad(tach_x+HUD_ICON_X,tach_y+HUD_H-HUD_ICON_Y-19,17,19,0xFFFFFFFF,
                &s_hud_textures[6],&s_hud_texture_uv[6]);
    }
    int shown_speed = (int)(vehicle_speed_kmh(vehicle) + .5f);
    if (shown_speed < 0) shown_speed = 0;
    if (shown_speed > 999) shown_speed = 999;

/* source top=60, height=12  ->  y = 96 - 60 - 12 = 24 */
const float speed_y = tach_y + HUD_H - 60.0f - 12.0f;

for (int i = 0; i < 3; i++) {
    int divisor = (i == 0) ? 100 : ((i == 1) ? 10 : 1);
    ui_hud_glyph(tach_x + 29.0f + i * 9.5f, speed_y, 9.5f, 12.0f,
                 (shown_speed / divisor) % 10,
                 &s_hud_textures[4], &s_hud_texture_uv[4],
                 12.2f, 122.0f);
}

/* source top=28, height=12  ->  y = 96 - 28 - 12 = 56 */
if (vehicle->gear >= 1 && vehicle->gear <= 6) {
    const float gear_y = tach_y + HUD_H - 28.0f - 12.0f;
    ui_hud_glyph(tach_x + 37.0f, gear_y, 12.0f, 12.0f, vehicle->gear - 1,
                 &s_hud_textures[3], &s_hud_texture_uv[3],
                 11.625f, 93.0f);
}
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C2D_SceneBegin(core_top_target());
    C2D_TextBufClear(s_text_buffer); C2D_Prepare();
    /* The map and streamer consume the same recovered road graph.  Convert
       nearby links to the compact bezel space around the player. */
    /* Standalone minimap: it deliberately has no placeholder label, full-map
       background, or navigation route.  Missions will supply a route later. */
    const float map_x=48.0f, map_y=195.0f;
    const float map_range=250.0f, map_scale=0.118f;
    /* Geometry and navigation centre-lines are close, but not identical.
       Snap the HUD marker to its closest link so it cannot drift off-map. */
    float marker_world_x=vehicle->x, marker_world_z=vehicle->z;
    float vehicle_fx, vehicle_fz, marker_fx=0.0f, marker_fz=1.0f;
    vehicle_forward(vehicle, &vehicle_fx, &vehicle_fz);
    if (road_network_ready(roads)) {
        int matched=road_network_match_point(roads, vehicle->x, vehicle->z, vehicle->y,
                                             vehicle_fx, vehicle_fz, s_minimap_matched_link,
                                             &marker_world_x, &marker_world_z,
                                             &marker_fx, &marker_fz);
        if (matched >= 0) s_minimap_matched_link=matched;
    } else s_minimap_matched_link=-1;
    float anchor_x=marker_world_x, anchor_z=marker_world_z;
    if (!s_minimap_origin_valid) {
        s_minimap_origin_x=anchor_x;
        s_minimap_origin_z=anchor_z;
        s_minimap_origin_valid=1;
    }
    /* Follow only the excess past the safe centre. This makes the car move
       over the road map, then keeps it inside the circular display. */
    float follow_x=marker_world_x-s_minimap_origin_x;
    float follow_z=marker_world_z-s_minimap_origin_z;
    float follow_distance=sqrtf(follow_x*follow_x+follow_z*follow_z);
    const float follow_radius=map_range*.43f;
    if (follow_distance > follow_radius) {
        float shift=(follow_distance-follow_radius)/follow_distance;
        s_minimap_origin_x += follow_x*shift;
        s_minimap_origin_z += follow_z*shift;
    }
    /* Carbon's map reads as a recessed charcoal instrument rather than a
       bright GPS panel: an outer lip, then a slightly lighter road field. */
    C2D_DrawCircleSolid(map_x,map_y,.455f,31.0f,C2D_Color32(2,5,7,248));
    C2D_DrawCircleSolid(map_x,map_y,.46f,29.4f,C2D_Color32(12,19,21,242));
    RoadNetworkLink links[128];
    int link_count=road_network_local_links(roads, s_minimap_origin_x, s_minimap_origin_z,
                                             map_range, links, 128);
    for (int i=0; i<link_count; ++i) {
        const RoadNetworkNode *a=&roads->nodes[links[i].a], *b=&roads->nodes[links[i].b];
        float adx=a->x-s_minimap_origin_x, adz=a->z-s_minimap_origin_z;
        float bdx=b->x-s_minimap_origin_x, bdz=b->z-s_minimap_origin_z;
        float min_y=fminf(a->y,b->y)-14.0f, max_y=fmaxf(a->y,b->y)+14.0f;
        if (vehicle->y < min_y || vehicle->y > max_y ||
            adx*adx+adz*adz > map_range*map_range || bdx*bdx+bdz*bdz > map_range*map_range) continue;
        float ax=map_x+adx*map_scale, ay=map_y-adz*map_scale;
        float bx=map_x+bdx*map_scale, by=map_y-bdz*map_scale;
        /* Narrow cool-grey routes with a black undercut match the original
           low-contrast road ink and stay readable on the dark dial. */
        C2D_DrawLine(ax, ay, C2D_Color32(1,3,4,255),
                     bx, by, C2D_Color32(1,3,4,255), 2.2f, .47f);
        C2D_DrawLine(ax, ay, C2D_Color32(151,164,164,232),
                     bx, by, C2D_Color32(151,164,164,232), .72f, .475f);
    }
    float marker_x=map_x+(marker_world_x-s_minimap_origin_x)*map_scale;
    float marker_y=map_y-(marker_world_z-s_minimap_origin_z)*map_scale;
    /* Draw the RGBA marker in the same Citro2D coordinate space as streets.
       This makes its projected position exact instead of approximating an
       offset between PICA and Citro2D. */
    if (s_gps_player_loaded) {
        C2D_Image marker_image={&s_gps_player, &s_gps_player_subtex};
        float marker_angle=atan2f(marker_fx, marker_fz);
        C2D_DrawImageAtRotated(marker_image, marker_x, marker_y, .49f,
                                marker_angle, NULL, .75f, .75f);
    }
    char top[32];
    snprintf(top,sizeof(top),"FREE DRIVE   N2O %3d%%",(int)(vehicle->nitro*100));
    draw_ui_text(top,127,13,.35f,C2D_Color32(230,245,235,255));
    C2D_Flush();

}

static void paint_picker_position(int index, float *x, float *y)
{
    int offset, count; float radius;
    if (index < 24) { offset=0; count=24; radius=84.0f; }
    else if (index < 40) { offset=24; count=16; radius=55.0f; }
    else { offset=40; count=8; radius=26.0f; }
    float angle=6.28318530718f*(float)(index-offset)/(float)count-1.57079632679f;
    *x=160.0f+cosf(angle)*radius; *y=130.0f+sinf(angle)*radius;
}

void renderer_draw_paint_picker(const uint32_t *colors, int count, int selected)
{
    C3D_RenderTarget *target=core_bottom_target();
    if (!target || !colors || count < 1 || !s_text_buffer) return;
    C2D_TargetClear(target,C2D_Color32(16,24,32,255));
    C2D_SceneBegin(target); C2D_TextBufClear(s_text_buffer); C2D_Prepare();
    C2D_DrawRectSolid(8,8,.40f,304,32,C2D_Color32(8,16,24,232));
    C2D_DrawCircleSolid(160,130,.41f,98,C2D_Color32(13,23,33,245));
    C2D_DrawCircleSolid(160,130,.42f,68,C2D_Color32(20,32,44,255));
    C2D_DrawCircleSolid(160,130,.43f,38,C2D_Color32(10,17,25,255));
    for (int i=0;i<count && i<48;i++) {
        float x,y; paint_picker_position(i,&x,&y);
        uint32_t rgb=colors[i];
        if (i==selected) C2D_DrawCircleSolid(x,y,.44f,11.0f,C2D_Color32(80,235,255,255));
        C2D_DrawCircleSolid(x,y,.45f,i==selected ? 8.0f : 7.0f,
                            C2D_Color32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255));
    }
    draw_ui_text("PAINT PALETTE",16,16,.55f,C2D_Color32(230,245,255,255));
    draw_ui_text("TOUCH A COLOR FOR PREVIEW",63,222,.34f,C2D_Color32(150,220,250,255));
    C2D_Flush();
}

void renderer_draw_main_menu(int selected_item)
{
    if (!s_ui_quad || s_frontend_texture_count != 6 || !s_text_buffer)
        return;
    C3D_Mtx projection, identity;
    begin_ui(&projection, &identity);
    ui_quad(0,0,400,240,0xFFFFFFFF,&s_frontend_textures[0],&s_frontend_texture_uv[0]);
    /* Generated assets deliberately separate the neutral five-row grid
       from the red selection plate.  Only the overlay moves per input. */
    ui_quad(0,0,400,240,0xFFFFFFFF,&s_frontend_textures[4],&s_frontend_texture_uv[4]);
    if (selected_item < 0 || selected_item > 4) selected_item=0;
    /* Tight 256x64 asset: its transparent crop is independent from the
       full-screen grid and therefore aligns exactly with one menu row. */
    /* The generated plate retains a transparent safety margin on its right.
       Stretch it to the exact visible span of a gray row; its left edge and
       vertical center stay locked to the grid. */
    ui_quad(190,161.0f-selected_item*29.0f,220,34,0xFFFFFFFF,
            &s_frontend_textures[5],&s_frontend_texture_uv[5]);
    ui_quad(21,14,236,59,0xFFFFFFFF,&s_frontend_textures[1],&s_frontend_texture_uv[1]);
    C2D_SceneBegin(core_top_target());
    C2D_TextBufClear(s_text_buffer);
    C2D_Prepare();
    draw_ui_text("MAIN MENU",300,36,0.30f,C2D_Color32(185,190,190,255));
    static const char *const menu_items[] = {"GARAGE", "OWN THE CITY", "QUICK PLAY", "EXTRAS", "EXIT GAME"};
    /* Citro2D text coordinates start at the physical top, unlike the
       PICA quad projection above.  Keep both systems on the same row. */
    for (int i=0;i<5;i++)
        draw_ui_text(menu_items[i],246,56+i*29,0.48f,
                     i==selected_item ? C2D_Color32(255,255,255,255) : C2D_Color32(205,205,205,255));
    draw_ui_text("D-PAD  MOVE",246,214,0.32f,C2D_Color32(176,190,190,255));
    draw_ui_text("A  SELECT",320,214,0.32f,C2D_Color32(255,255,255,255));
    C2D_Flush();
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
}

void renderer_set_vehicle_debug(int enabled)
{
    s_vehicle_debug = !!enabled;
}

int renderer_vehicle_debug(void)
{
    return s_vehicle_debug;
}

void renderer_shutdown(void)
{
    if (s_vehicle_stream) {
        C3D_FrameSync();
        linearFree(s_vehicle_stream);
        s_vehicle_stream = NULL;
        s_vehicle_stream_capacity = 0;
    }
    if (s_ground)
        linearFree(s_ground);
    if (s_road)
        linearFree(s_road);
    if (s_wheel_stream)
        linearFree(s_wheel_stream);
    s_wheel_stream = NULL;
    s_wheel_stream_count = s_wheel_tire_offset = s_wheel_rim_offset = 0;
    if (s_wheel_tire_mesh)
        n3p_free(s_wheel_tire_mesh);
    s_wheel_tire_mesh = NULL;
    if (s_wheel_faces)
        linearFree(s_wheel_faces);
    if (s_wheel_tread)
        linearFree(s_wheel_tread);
    if (s_wheel_rim_faces)
        linearFree(s_wheel_rim_faces);
    if (s_ui_quad)
        linearFree(s_ui_quad);
    if (s_effect_stream)
        linearFree(s_effect_stream);
    for (int i=0;i<s_garage_icon_count;i++)
        C3D_TexDelete(&s_garage_icons[i]);
    for (int i=0;i<s_performance_icon_count;i++)
        C3D_TexDelete(&s_performance_icons[i]);
    if (s_garage_car_icon_loaded)
        C3D_TexDelete(&s_garage_car_icon);
    for (int i=0;i<s_frontend_texture_count;i++)
        C3D_TexDelete(&s_frontend_textures[i]);
    for (int i=0;i<s_effect_texture_count;i++) C3D_TexDelete(&s_effect_textures[i]);
    for (int i=0;i<s_hud_texture_count;i++) C3D_TexDelete(&s_hud_textures[i]);
    if (s_gps_player_loaded) C3D_TexDelete(&s_gps_player);
    if (s_sky_loaded) C3D_TexDelete(&s_sky_texture);
    s_gps_player_loaded = s_sky_loaded = 0;
    if (s_text_buffer)
        C2D_TextBufDelete(s_text_buffer);
    s_text_buffer = NULL;
    core_destroy_bottom_target();
    if (s_c2d_initialized)
        C2D_Fini();
    s_c2d_initialized = 0;
    s_ground = NULL;
    s_road = NULL;
    s_wheel_faces = NULL;
    s_wheel_rim_faces = NULL;
    s_wheel_tread = NULL;
    s_ui_quad = NULL;
    s_effect_stream = NULL;
    s_garage_icon_count = 0;
    s_performance_icon_count = 0;
    s_garage_car_icon_loaded = 0;
    s_frontend_texture_count = 0;
    s_effect_texture_count = 0;
    s_road_count = 0;

    if (s_scene_dvlb) {
        shaderProgramFree(&s_scene_program);
        DVLB_Free(s_scene_dvlb);
        s_scene_dvlb = NULL;
    }
    shaderProgramFree(&s_program);
    if (s_dvlb)
        DVLB_Free(s_dvlb);
    s_dvlb = NULL;
}
