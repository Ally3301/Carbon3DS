#include "n3p_loader.h"
#include "resources.h"
#include "track.h"
#include "wav.h"
#include "vehicle_material_policy.h"
#include "gpu_color.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *resource_load(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    rewind(fp);
    if (n < 0 || n > 16000000) {
        fclose(fp);
        return NULL;
    }
    void *b = malloc(n ? (size_t)n : 1);
    assert(b);
    assert(fread(b, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    *size = (size_t)n;
    return b;
}

static void assert_near(float a, float b)
{
    assert(fabsf(a - b) < 0.00001f);
}

static void test_gpu_color_packing(void)
{
    uint32_t c = gpu_rgba8(0xB7, 0x43, 0x32, 0xFF);
    assert(c == 0xFF3243B7u);
    assert(gpu_rgba8_r(c) == 0xB7);
    assert(gpu_rgba8_g(c) == 0x43);
    assert(gpu_rgba8_b(c) == 0x32);
    assert(gpu_rgba8_a(c) == 0xFF);

    /* Regression: the old literal looked like RRGGBBAA but decoded as
       R=FF,G=32,B=43,A=B7 in the PICA/C2D byte layout. */
    c = 0xB74332FFu;
    assert(gpu_rgba8_r(c) == 0xFF);
    assert(gpu_rgba8_a(c) == 0xB7);
}

static void test_vehicle_material_policy(void)
{
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 1500, 0)
           == VEHICLE_MAT_PAINT);
    assert(vehicle_material_classify(N3P_RENDER_WINDOW, 1501, 1)
           == VEHICLE_MAT_WINDOW);
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 1000, 1)
           == VEHICLE_MAT_CARBON);
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 1001, 1)
           == VEHICLE_MAT_UNDERBODY);
    assert(vehicle_material_classify(N3P_RENDER_GOURAUD, 0xffff, 0)
           == VEHICLE_MAT_GOURAUD);
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 9999, 0)
           == VEHICLE_MAT_SPECIAL_DYNAMIC);
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 3, 1)
           == VEHICLE_MAT_TEXTURED);
    assert(vehicle_material_classify(N3P_RENDER_TEXTURE_SHINY, 42, 0)
           == VEHICLE_MAT_UNTEXTURED);

    assert(vehicle_material_uv_mode(VEHICLE_MAT_TEXTURED) == VEHICLE_UV_CLAMP);
    assert(vehicle_material_uv_mode(VEHICLE_MAT_WINDOW) == VEHICLE_UV_CLAMP);
    assert(vehicle_material_uv_mode(VEHICLE_MAT_UNDERBODY) == VEHICLE_UV_CLAMP);
    assert(vehicle_material_uv_mode(VEHICLE_MAT_CARBON) == VEHICLE_UV_REPEAT);
    assert(vehicle_material_uv_mode(VEHICLE_MAT_PAINT) == VEHICLE_UV_NONE);
}

static void test_texture_uv(void)
{
    TextureUV uv;
    float u, v;

    texture_uv_identity(&uv);
    texture_uv_apply(&uv, 0.25f, 0.75f, &u, &v);
    assert_near(u, 0.25f);
    assert_near(v, 0.75f);

    /* Ordinary tex3ds subtexture: source (0,0) is logical top-left. */
    texture_uv_from_corners(&uv,
                            0.10f, 0.90f,
                            0.50f, 0.90f,
                            0.10f, 0.20f);
    texture_uv_apply(&uv, 0.50f, 0.25f, &u, &v);
    assert_near(u, 0.30f);
    assert_near(v, 0.725f);

    /* Rotated atlas entry: U advances along backing V, V along backing U. */
    texture_uv_from_corners(&uv,
                            0.20f, 0.30f,
                            0.20f, 0.70f,
                            0.80f, 0.30f);
    texture_uv_apply(&uv, 0.25f, 0.75f, &u, &v);
    assert_near(u, 0.65f);
    assert_near(v, 0.40f);
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "part")) {
        N3PPart *p = n3p_load(argv[2]);
        if (!p)
            return 2;
        assert(p->vertex_count > 0);
        assert(p->index_count % 3 == 0);
        assert(p->group_count > 0);
        assert(p->draw_verts);
        assert(p->draw_vertex_count == p->index_count);
        for (uint32_t i = 0; i < p->index_count; ++i) {
            const N3PVertex *a = &p->draw_verts[i];
            const N3PVertex *b = &p->verts[p->indices[i]];
            assert(!memcmp(a, b, sizeof(*a)));
        }
        n3p_free(p);
        return 0;
    }

    if (argc > 2 && !strcmp(argv[1], "wav")) {
        FILE *f = fopen(argv[2], "rb");
        assert(f);
        WavInfo w;
        int rc = wav_read_header(f, &w);
        fclose(f);
        return rc ? 2 : 0;
    }

    test_texture_uv();
    test_vehicle_material_policy();
    test_gpu_color_packing();

    Vehicle v;
    InputState in = {.throttle = 1};
    vehicle_reset(&v);
    for (int i = 0; i < 3600; ++i)
        vehicle_update(&v, &in, 1.0f/60);
    assert(v.speed > 40 && v.speed <= 48 && isfinite(v.z));

    in.nitro = 1;
    for (int i = 0; i < 180; ++i)
        vehicle_update(&v, &in, 1.0f/60);
    assert(v.nitro >= 0 && v.nitro < 0.2f);

    in = (InputState){.brake = 1};
    for (int i = 0; i < 600; ++i)
        vehicle_update(&v, &in, 1.0f/60);
    assert(v.speed < -1.0f && v.gear == -1);
    in = (InputState){.throttle = 1};
    for (int i = 0; i < 180; ++i)
        vehicle_update(&v, &in, 1.0f/60);
    assert(v.speed > 0.5f && v.gear >= 1);

    vehicle_reset(&v);
    v.speed = 10;
    in = (InputState){.steer = 1};
    vehicle_update(&v, &in, 1.0f/60);
    assert(v.x < 0);

    Race r;
    race_reset(&r, &v);
    r.countdown = 0;
    v.x = 0;
    v.z = 0;
    race_update(&r, &v, 1.0f/60);
    assert(isfinite(v.x) && hypotf(v.x, v.z) > 85);

    race_reset(&r, &v);
    r.countdown = 0;
    for (int i = 0; i < 20; ++i) {
        v.x = TRACK_RADIUS;
        v.z = 0;
        v.speed = 10;
        race_update(&r, &v, 1.0f/60);
    }
    assert(r.lap == 1 && r.checkpoint == 1);

    for (int lap = 0; lap < TRACK_LAPS; ++lap)
        for (int cp = 1; cp <= 4; ++cp) {
            float a = cp * 1.57079632679f;
            v.x = TRACK_RADIUS * cosf(a);
            v.z = TRACK_RADIUS * sinf(a);
            v.yaw = -a;
            v.speed = 10;
            race_update(&r, &v, 1.0f/60);
        }
    assert(r.finished && r.lap == 3 && r.best_lap > 0);

    puts("physics and ordered checkpoint tests passed");
    return 0;
}
