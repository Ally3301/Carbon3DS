#include "game.h"
#include "core.h"
#include "input.h"
#include "renderer.h"
#include "vehicle.h"
#include "vehicle_asset.h"
#include "camera.h"
#include "track.h"
#include "audio.h"
#include "engine_audio.h"
#include "gpu_color.h"
#include "world_stream.h"

#include <3ds.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_CARS 64
#define GARAGE_SLOT_COUNT 5
#define PROFILE_MAGIC 0x4E465333u /* NFS3 */
#define PROFILE_VERSION 3u
#define PROFILE_PATH "sdmc:/3ds/nfs3ds/profile.bin"

static char cars[MAX_CARS][32];
static int car_count, selected, paused;
static enum { MENU, GARAGE, RACE } screen;
static VehicleAsset car;
static SceneAsset garage_scene;
static WorldStream palmont;
static Vehicle vehicle;
static Camera camera;
static Race race;
static float accumulator, hud_time;
static char message[64];
static int garage_slot;
static int garage_paint_preview = -1;
static uint32_t garage_paint_preview_rgb;
static int garage_detail_open;
static float wall_impact_cooldown;
static int nitro_loop_playing;
static int skid_loop_playing;
static float safe_x, safe_y, safe_z;
static int safe_position_valid;
static float garage_motion;
static VehicleAsset garage_preview[4];
static int garage_preview_index[4];
static int garage_preview_slot[4];
static int dealer_index;

/* The exported vehicle meshes and wheels share local coordinates.  Use the
 * fitted axle centres to calculate the body origin from tyre contact rather
 * than assuming every car can use the former 0.18 m offset. */
static void vehicle_configure_wheels(void)
{
    float front_radius, rear_radius, front_y, rear_y;
    vehicle_asset_wheel_axles(&car, &front_radius, &rear_radius, &front_y, &rear_y);
    (void)front_y; (void)rear_y;
    vehicle_set_wheel_radius(&vehicle, (front_radius + rear_radius) * 0.39f);
}

static int vehicle_update_road_support(float dt)
{
    float front_x, rear_x, track, ignored_radius, ignored_center;
    float front_radius, rear_radius, front_y, rear_y;
    vehicle_asset_wheel_placement(&car, &front_x, &rear_x, &track,
                                  &ignored_radius, &ignored_center);
    vehicle_asset_wheel_axles(&car, &front_radius, &rear_radius, &front_y, &rear_y);
    (void)track; (void)ignored_radius; (void)ignored_center;

    /* Renderer applies this scale to the imported tyre, so collision uses the
       same effective radius and the contact surface agrees with the picture. */
    front_radius *= 0.78f;
    rear_radius *= 0.78f;
    front_y -= front_radius * 0.03f;
    rear_y -= rear_radius * 0.03f;

    float forward_x, forward_z;
    vehicle_forward(&vehicle, &forward_x, &forward_z);
    const float axle_x[2] = {front_x, rear_x};
    const float tyre_bottom[2] = {front_y - front_radius, rear_y - rear_radius};
    float support = -1e30f;
    float axle_support[2] = {-1e30f, -1e30f};
    int contacts = 0;
    for (int axle = 0; axle < 2; ++axle) {
        float ground_y;
        float x = vehicle.x + forward_x * axle_x[axle];
        float z = vehicle.z + forward_z * axle_x[axle];
        /* Ray starts just above the chassis.  It can climb a steep road, but
           cannot select tunnel roofs or the mountain geometry above them. */
        if (world_stream_ground_y(&palmont, x, z, vehicle.y + 0.80f, &ground_y)) {
            float required = ground_y - tyre_bottom[axle] + 0.018f;
            /* A valid road can rise into the wheel's look-ahead range, but a
               much higher hit is a wall/building face and must not become a
               climbable floor. */
            if (required <= vehicle.y + 0.55f) {
                axle_support[axle] = required;
                if (required > support) support = required;
                contacts++;
            }
        }
    }
    if (support > -1e20f)
        vehicle_suspension_update(&vehicle, support, dt);

    /* Axle support gives the chassis its grade on both ascents and descents.
       Retain the previous grade briefly if streaming has not yet delivered
       the next cell, preventing a visual snap at a cell boundary. */
    if (axle_support[0] > -1e20f && axle_support[1] > -1e20f) {
        float wheelbase = front_x - rear_x;
        if (wheelbase > 0.2f) {
            float grade = atan2f(axle_support[0] - axle_support[1], wheelbase);
            if (grade > 0.095f) grade = 0.095f;
            if (grade < -0.095f) grade = -0.095f;
            vehicle.terrain_pitch += (grade - vehicle.terrain_pitch) * fminf(1.0f, dt * 7.0f);
        }
    }
    return contacts == 2;
}

typedef struct VehicleCustomization {
    int bodykit_index;
    int hood_index;
    int spoiler_index;
    int wheel_style;
    int paint_index;
    uint32_t paint_rgb;
    int valid;
} VehicleCustomization;

typedef struct GarageProfile {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_count;
    int active_slot;
    int garage_car[GARAGE_SLOT_COUNT];
    uint32_t cash;
    uint32_t rep;
    unsigned char performance[MAX_CARS][5];
    VehicleCustomization customizations[MAX_CARS];
} GarageProfile;

static GarageProfile profile;
static VehicleCustomization customizations[MAX_CARS];

static const uint32_t paint_palette_rgb[] = {
    0xE53935,0xEF5350,0xFB8C00,0xF9A825,0xFDD835,0xC0CA33,0x7CB342,0x43A047,
    0x00897B,0x00ACC1,0x039BE5,0x1E88E5,0x3949AB,0x5E35B1,0x8E24AA,0xD81B60,
    0x5D4037,0x795548,0x9E9E9E,0xECEFF1,0x263238,0x111111,0x6D1B1B,0x7A4F12,
    0xB71C1C,0xC62828,0xE65100,0xFF6F00,0xF57F17,0x827717,0x33691E,0x1B5E20,
    0x004D40,0x006064,0x01579B,0x0D47A1,0x1A237E,0x311B92,0x4A148C,0x880E4F,
    0x4E342E,0x37474F,0xB0BEC5,0xFFFFFF,0x212121,0x000000,0x8B0000,0xC46B00
};

static uint32_t paint_from_rgb(uint32_t rgb)
{
    return ((rgb >> 16) & 255) | (rgb & 0x00FF00) | ((rgb & 255) << 16) | 0xFF000000u;
}

static int paint_palette_index(uint32_t rgb)
{
    for (int i=0;i<(int)(sizeof(paint_palette_rgb)/sizeof(paint_palette_rgb[0]));i++)
        if (paint_palette_rgb[i] == rgb) return i;
    return 0;
}

static void paint_palette_position(int index, float *x, float *y)
{
    int offset, count; float radius;
    if (index < 24) { offset=0; count=24; radius=84.0f; }
    else if (index < 40) { offset=24; count=16; radius=55.0f; }
    else { offset=40; count=8; radius=26.0f; }
    float angle=6.28318530718f*(float)(index-offset)/(float)count-1.57079632679f;
    *x=160.0f+cosf(angle)*radius; *y=130.0f+sinf(angle)*radius;
}

static int paint_touch_index(const InputState *in)
{
    if (!in->touch_down) return -1;
    int best=-1; float best_d2=121.0f;
    for (int i=0;i<(int)(sizeof(paint_palette_rgb)/sizeof(paint_palette_rgb[0]));i++) {
        float x,y,dx,dy; paint_palette_position(i,&x,&y);
        dx=(float)in->touch_x-x; dy=(float)in->touch_y-y;
        float d2=dx*dx+dy*dy;
        if (d2 < best_d2) { best=i; best_d2=d2; }
    }
    return best;
}

typedef enum GarageView {
    GARAGE_FLEET,
    GARAGE_TO_CUSTOMIZE,
    GARAGE_CUSTOMIZE,
    GARAGE_TO_FLEET,
    GARAGE_TO_DEALERSHIP,
    GARAGE_DEALERSHIP
} GarageView;
static GarageView garage_view;
static float garage_transition;
static int garage_transition_swapped;
/* Main-menu item 0 is the currently implemented Garage entry. */
static int main_menu_item;

typedef enum GarageCategory {
    GARAGE_ENGINE, GARAGE_TURBO, GARAGE_CHASSIS, GARAGE_HANDLING, GARAGE_NITRO,
    GARAGE_BODYKIT, GARAGE_HOOD, GARAGE_SPOILER, GARAGE_PAINT, GARAGE_RIMS,
    GARAGE_CATEGORY_COUNT
} GarageCategory;

#define GARAGE_PERFORMANCE_COUNT 5
#define GARAGE_VISUAL_CATEGORY(slot) ((slot) - GARAGE_PERFORMANCE_COUNT)

static const VehiclePartSlot garage_part_slots[] = {
    VEHICLE_SLOT_BODY,
    VEHICLE_SLOT_HOOD,
    VEHICLE_SLOT_SPOILER,
};

static void save_customization(int index)
{
    if (index < 0 || index >= car_count || !car.id[0])
        return;
    VehicleCustomization *c = &customizations[index];
    c->bodykit_index = vehicle_asset_selected_value(&car, VEHICLE_SLOT_BODY);
    c->hood_index = vehicle_asset_selected_value(&car, VEHICLE_SLOT_HOOD);
    c->spoiler_index = vehicle_asset_selected_value(&car, VEHICLE_SLOT_SPOILER);
    c->wheel_style = vehicle_asset_wheel_style(&car);
    c->valid = 1;
}

static void apply_customization(VehicleAsset *asset, int index)
{
    VehicleCustomization *c = &customizations[index];
    if (!c->valid) {
        c->bodykit_index = vehicle_asset_selected_value(asset, VEHICLE_SLOT_BODY);
        c->hood_index = vehicle_asset_selected_value(asset, VEHICLE_SLOT_HOOD);
        c->spoiler_index = vehicle_asset_selected_value(asset, VEHICLE_SLOT_SPOILER);
        c->wheel_style = vehicle_asset_wheel_style(asset);
        c->paint_index = 0;
        c->paint_rgb = paint_palette_rgb[0];
        c->valid = 1;
    } else {
        (void)vehicle_asset_select_value(asset, VEHICLE_SLOT_BODY, c->bodykit_index);
        if (c->hood_index >= 0)
            (void)vehicle_asset_select_value(asset, VEHICLE_SLOT_HOOD, c->hood_index);
        if (c->spoiler_index >= 0)
            (void)vehicle_asset_select_value(asset, VEHICLE_SLOT_SPOILER, c->spoiler_index);
        (void)vehicle_asset_select_wheel(asset, c->wheel_style);
    }
    vehicle_asset_set_paint(asset, paint_from_rgb(c->paint_rgb ? c->paint_rgb : paint_palette_rgb[0]));
}

static int car_index_named(const char *id)
{
    for (int i=0;i<car_count;++i)
        if (!strcmp(cars[i],id)) return i;
    return -1;
}

static void profile_defaults(void)
{
    memset(&profile,0,sizeof(profile));
    profile.magic=PROFILE_MAGIC;
    profile.version=PROFILE_VERSION;
    profile.slot_count=GARAGE_SLOT_COUNT;
    profile.active_slot=0;
    for (int i=0;i<GARAGE_SLOT_COUNT;++i) profile.garage_car[i]=-1;
    /* A complete starter showcase makes the five physical garage bays useful
       immediately. Future career rewards can replace these slots. */
    static const char *const starter[] = {"SKYLINE","350Z","RX7","LANCER","SUPRA"};
    for (int i=0;i<GARAGE_SLOT_COUNT;++i) {
        int found=car_index_named(starter[i]);
        profile.garage_car[i]=found >= 0 ? found : i % car_count;
    }
    profile.cash=25000;
    profile.rep=0;
    memset(customizations,0,sizeof(customizations));
}

static void profile_validate(void)
{
    if (profile.active_slot < 0 || profile.active_slot >= GARAGE_SLOT_COUNT)
        profile.active_slot=0;
    for (int i=0;i<GARAGE_SLOT_COUNT;++i)
        if (profile.garage_car[i] < 0 || profile.garage_car[i] >= car_count)
            profile.garage_car[i]=-1;
    if (profile.garage_car[profile.active_slot] < 0)
        profile_defaults();
}

static void profile_load(void)
{
    GarageProfile disk;
    FILE *fp=fopen(PROFILE_PATH,"rb");
    if (!fp || fread(&disk,sizeof(disk),1,fp) != 1 || disk.magic != PROFILE_MAGIC
        || disk.version != PROFILE_VERSION || disk.slot_count != GARAGE_SLOT_COUNT) {
        if (fp) fclose(fp);
        profile_defaults();
        return;
    }
    fclose(fp);
    profile=disk;
    memcpy(customizations,profile.customizations,sizeof(customizations));
    profile_validate();
}

static void profile_save(void)
{
    profile.magic=PROFILE_MAGIC;
    profile.version=PROFILE_VERSION;
    profile.slot_count=GARAGE_SLOT_COUNT;
    memcpy(profile.customizations,customizations,sizeof(customizations));
    (void)mkdir("sdmc:/3ds",0777);
    (void)mkdir("sdmc:/3ds/nfs3ds",0777);
    FILE *fp=fopen(PROFILE_PATH,"wb");
    if (!fp) return;
    (void)fwrite(&profile,sizeof(profile),1,fp);
    fclose(fp);
}

/* The player camera sits in front of the selected bay; the other four cars
   recede behind it like a showroom line rather than clipping the foreground. */
static const float garage_bay_x[GARAGE_SLOT_COUNT]={0.0f,-3.15f,3.15f,-5.45f,5.45f};
static const float garage_bay_z[GARAGE_SLOT_COUNT]={0.0f,-1.35f,-1.35f,-2.85f,-2.85f};
static const float garage_bay_yaw[GARAGE_SLOT_COUNT]={0.0f,-.20f,.20f,-.34f,.34f};

static float garage_floor_y(const VehicleAsset *asset, float scale)
{
    float fr,rr,fy,ry;
    vehicle_asset_wheel_axles(asset,&fr,&rr,&fy,&ry);
    fr*=.78f; rr*=.78f;
    float bottom=fminf(fy-fr*.97f,ry-rr*.97f);
    return fmaxf(.04f,fminf(.72f,.025f-bottom*scale));
}

static void garage_place_active(void)
{
    int slot=profile.active_slot;
    vehicle.x=garage_bay_x[slot];
    vehicle.y=garage_floor_y(&car,1.0f);
    vehicle.z=garage_bay_z[slot];
}

/* Side workshop bay: inside the recovered floor envelope (x -19..19,
   z 0..8), away from the five-car showroom line. */
static void garage_place_workshop(void)
{
    vehicle.x=10.8f;
    vehicle.y=garage_floor_y(&car,1.0f);
    vehicle.z=4.6f;
}

static int garage_show_fleet(void)
{
    return garage_view == GARAGE_FLEET
        || (garage_view == GARAGE_TO_CUSTOMIZE && garage_transition < .5f)
        || (garage_view == GARAGE_TO_DEALERSHIP && garage_transition < .5f)
        || (garage_view == GARAGE_TO_FLEET && garage_transition >= .5f);
}

/* Internal slot ids are not spatially sorted. Navigation must follow the
   left-to-right showroom order users see, rather than raw profile indexes. */
static int garage_adjacent_slot(int current, int direction)
{
    static const int visual_order[GARAGE_SLOT_COUNT]={3,1,0,2,4};
    int at=0;
    for (;at<GARAGE_SLOT_COUNT;++at)
        if (visual_order[at] == current) break;
    if (at == GARAGE_SLOT_COUNT) return current;
    return visual_order[(at + GARAGE_SLOT_COUNT + (direction > 0 ? 1 : -1))
                        % GARAGE_SLOT_COUNT];
}

static int garage_first_occupied(void)
{
    for (int i=0;i<GARAGE_SLOT_COUNT;++i)
        if (profile.garage_car[i] >= 0 && profile.garage_car[i] < car_count) return i;
    return -1;
}

const char *game_screen_name(void)
{
    return screen == MENU ? "Menu" : screen == GARAGE ? "Garage" : "Time trial";
}

static void garage_previews_free(void)
{
    for (int i=0;i<4;++i) {
        vehicle_asset_free(&garage_preview[i]);
        garage_preview_index[i]=-1;
        garage_preview_slot[i]=-1;
    }
}

static void garage_previews_refresh(void)
{
    garage_previews_free();
    int out=0;
    for (int offset=1;offset<GARAGE_SLOT_COUNT;++offset) {
        int slot=(profile.active_slot+offset)%GARAGE_SLOT_COUNT;
        int index=profile.garage_car[slot];
        if (index < 0 || index >= car_count) continue;
        garage_preview_index[out]=index;
        garage_preview_slot[out]=slot;
        /* The garage has a fixed five-car budget. Keep neighbour cars fully
           textured and wheeled so the fleet reads as real, not silhouettes. */
        if (vehicle_asset_load(&garage_preview[out],cars[index]) == 0) {
            apply_customization(&garage_preview[out],index);
        }
        ++out;
    }
}

static int load_car(int index);

static unsigned dealer_price(int index)
{
    return 12000u + (unsigned)(index % 8) * 3500u;
}

/* All five fleet vehicles are already resident. Promoting a neighbour is an
   ownership swap, not a file load, so selection stays at 60 FPS. */
static int garage_focus_slot(int slot)
{
    if (slot < 0 || slot >= GARAGE_SLOT_COUNT || slot == profile.active_slot)
        return slot == profile.active_slot ? 0 : -1;
    int wanted=profile.garage_car[slot];
    for (int i=0;i<4;++i) {
        if (garage_preview_slot[i] != slot || garage_preview_index[i] != wanted)
            continue;
        VehicleAsset swap=car;
        car=garage_preview[i];
        garage_preview[i]=swap;
        int old_slot=profile.active_slot;
        garage_preview_slot[i]=old_slot;
        garage_preview_index[i]=profile.garage_car[old_slot];
        profile.active_slot=slot;
        selected=wanted;
        vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
        vehicle_configure_wheels();
        engine_audio_select_vehicle(cars[selected]);
        garage_place_active();
        return 0;
    }
    /* A failed/evicted preview is the only case that needs a slow fallback. */
    if (wanted >= 0 && load_car(wanted) == 0) {
        profile.active_slot=slot;
        garage_place_active();
        garage_previews_refresh();
        return 0;
    }
    return -1;
}

static int load_car(int index)
{
    if (index < 0 || index >= car_count)
        return -1;

    /* Loading an entire second car beside the old one exhausts Old 3DS
       linear memory after several garage changes.  Synchronize then release
       the old GPU resources before allocating its replacement. */
    C3D_FrameSync();
    vehicle_asset_free(&car);
    VehicleAsset next;
    vehicle_asset_init(&next);
    if (vehicle_asset_load(&next, cars[index]) != 0) {
        snprintf(message, sizeof(message), "Failed: %.40s", cars[index]);
        return -1;
    }
    apply_customization(&next, index);
    vehicle_set_performance_levels(&vehicle, profile.performance[index]);
    car = next;
    selected = index;
    vehicle_configure_wheels();
    engine_audio_select_vehicle(cars[index]);
    message[0] = 0;
    return 0;
}

int game_init(void)
{
    vehicle_asset_init(&car);

    FILE *fp = fopen("romfs:/vehicles.txt", "r");
    if (!fp)
        return -1;

    char line[128];
    while (car_count < MAX_CARS && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        size_t n = strlen(line);
        int valid = n > 0 && n < sizeof(cars[0]);
        for (size_t i = 0; i < n; ++i)
            if (!isalnum((unsigned char)line[i]) && line[i] != '_')
                valid = 0;
        if (valid)
            strcpy(cars[car_count++], line);
    }
    fclose(fp);

    if (!car_count)
        return -1;

    profile_load();
    selected=profile.garage_car[profile.active_slot];
    if (load_car(selected))
        return -1;

    screen = MENU;
    main_menu_item = 0;
    vehicle_reset(&vehicle);
    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
    safe_position_valid = 0;
    camera_init(&camera, CAM_ORBIT);
    audio_music("romfs:/audio/music.wav");
    engine_audio_init();
    return 0;
}

static void garage(void)
{
    engine_audio_set_active(0);
    world_stream_free(&palmont);
    if (!garage_scene.section && scene_asset_load(&garage_scene, "romfs:/world/garage.n3s")) {
        snprintf(message, sizeof(message), "PocketGarage load failed");
        return;
    }
    screen = GARAGE;
    paused = 0;
    accumulator = 0;
    garage_slot = GARAGE_ENGINE;
    garage_detail_open = 0;
    garage_view = GARAGE_FLEET;
    garage_transition = 0.0f;
    vehicle_reset(&vehicle);
    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
    safe_position_valid = 0;
    camera_init(&camera, CAM_ORBIT);
    garage_motion = 0.0f;
    garage_place_active();
    garage_previews_refresh();
    audio_pause(0);
    audio_play("romfs:/audio/menu.wav");
}

static const char *const garage_performance_names[] = {"ENGINE", "TURBO", "CHASSIS", "HANDLING", "NITRO"};
static const unsigned garage_upgrade_costs[] = {1200, 1800, 2800, 4200};

static void garage_customize(const InputState *in)
{
    if (!garage_detail_open) {
        if (in->slot_previous) garage_slot=(garage_slot+GARAGE_CATEGORY_COUNT-1)%GARAGE_CATEGORY_COUNT;
        if (in->slot_next) garage_slot=(garage_slot+1)%GARAGE_CATEGORY_COUNT;
        if (in->confirm) {
            garage_detail_open=1;
            garage_paint_preview=-1;
            garage_paint_preview_rgb=0;
            audio_play("romfs:/audio/menu.wav");
        }
        return;
    }

    if (garage_slot < GARAGE_PERFORMANCE_COUNT) {
        unsigned char *level = &profile.performance[selected][garage_slot];
        if (in->confirm) {
            if (*level >= 4) {
                snprintf(message, sizeof(message), "%s MAXED", garage_performance_names[garage_slot]);
            } else {
                unsigned price = garage_upgrade_costs[*level];
                if (profile.cash < price)
                    snprintf(message, sizeof(message), "NEED $%u", price);
                else {
                    profile.cash -= price;
                    ++*level;
                    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
                    profile_save();
                    audio_play("romfs:/audio/menu.wav");
                    snprintf(message, sizeof(message), "%s LEVEL %u INSTALLED",
                             garage_performance_names[garage_slot], (unsigned)*level);
                }
            }
        }
        return;
    }

    int visual = GARAGE_VISUAL_CATEGORY(garage_slot);
    int touch_color=visual == 3 ? paint_touch_index(in) : -1;
    if (touch_color >= 0) {
        garage_paint_preview=touch_color;
        garage_paint_preview_rgb=paint_palette_rgb[touch_color];
        vehicle_asset_set_paint(&car, paint_from_rgb(garage_paint_preview_rgb));
    }
    if (in->confirm) {
        if (visual == 3 && garage_paint_preview_rgb)
            customizations[selected].paint_rgb=garage_paint_preview_rgb;
        garage_paint_preview=-1;
        garage_paint_preview_rgb=0;
        save_customization(selected);
        profile_save();
        audio_play("romfs:/audio/menu.wav");
        snprintf(message,sizeof(message),"%s APPLIED", visual == 0 ? "BODY" : visual == 1 ? "HOOD" : visual == 2 ? "SPOILER" : visual == 3 ? "PAINT" : "RIMS");
        return;
    }
    int direction = in->next ? 1 : in->previous ? -1 : 0;
    if (!direction) return;
    int result;
    if (visual < 3) {
        VehiclePartSlot slot = garage_part_slots[visual];
        result = slot == VEHICLE_SLOT_BODY
            ? vehicle_asset_cycle_bodykit(&car, direction)
            : vehicle_asset_cycle_part(&car, slot, direction);
    } else if (visual == 3) {
        VehicleCustomization *c = &customizations[selected];
        int count=(int)(sizeof(paint_palette_rgb)/sizeof(paint_palette_rgb[0]));
        if (!garage_paint_preview_rgb) garage_paint_preview_rgb=c->paint_rgb ? c->paint_rgb : paint_palette_rgb[0];
        garage_paint_preview=(paint_palette_index(garage_paint_preview_rgb)+(direction > 0 ? 1 : -1)+count)%count;
        garage_paint_preview_rgb=paint_palette_rgb[garage_paint_preview];
        vehicle_asset_set_paint(&car, paint_from_rgb(garage_paint_preview_rgb));
        result = 0;
    } else {
        result = vehicle_asset_cycle_wheel(&car, direction);
    }
    if (result == 0) {
        audio_play("romfs:/audio/menu.wav");
        message[0] = 0;
    } else {
        snprintf(message, sizeof(message), "PART NOT AVAILABLE");
    }
}

void game_update(void)
{
    const InputState *in = input_get();
    float dt = core_dt();
    hud_time += dt;

    if (screen != RACE) {
        if (screen == MENU)
            camera.orbit_yaw += dt * 0.35f;

        if (in->back) {
            C3D_FrameSync();
            garage_previews_free();
            scene_asset_free(&garage_scene);
            screen = MENU;
        } else if (screen == MENU) {
            if (in->slot_previous || in->slot_next) {
                int direction=in->slot_next ? 1 : -1;
                main_menu_item=(main_menu_item + 5 + direction) % 5;
                audio_play("romfs:/audio/menu.wav");
            }
            if (in->confirm && main_menu_item == 0)
                garage();
        } else if (screen == GARAGE) {
            if (garage_view == GARAGE_FLEET) {
                if (in->next || in->previous) {
                    int step=in->next ? 1 : -1;
                    int slot=garage_adjacent_slot(profile.active_slot,step);
                    if (profile.garage_car[slot] >= 0) {
                        save_customization(selected);
                        if (garage_focus_slot(slot) == 0) {
                            profile_save();
                            audio_play("romfs:/audio/menu.wav");
                        }
                    }
                } else if (in->custom_previous) {
                    /* L sells/removes the focused car, then opens the dealer to
                       fill this now-empty physical bay. */
                    profile.garage_car[profile.active_slot]=-1;
                    profile_save();
                    garage_view=GARAGE_TO_DEALERSHIP;
                    garage_transition=0.0f; garage_transition_swapped=0;
                    dealer_index=0;
                    audio_play("romfs:/audio/menu.wav");
                } else if (in->pause) {
                    garage_view=GARAGE_TO_DEALERSHIP;
                    garage_transition=0.0f; garage_transition_swapped=0;
                    dealer_index=profile.garage_car[profile.active_slot];
                    if (dealer_index < 0) dealer_index=0;
                    audio_play("romfs:/audio/menu.wav");
                } else if (in->confirm && profile.garage_car[profile.active_slot] >= 0) {
                    garage_view=GARAGE_TO_CUSTOMIZE;
                    garage_transition=0.0f; garage_transition_swapped=0;
                    garage_slot=GARAGE_ENGINE;
                    garage_detail_open=0;
                    audio_play("romfs:/audio/menu.wav");
                }
            } else if (garage_view == GARAGE_TO_CUSTOMIZE || garage_view == GARAGE_TO_FLEET || garage_view == GARAGE_TO_DEALERSHIP) {
                /* The shutter hides the instantaneous context change. Moving
                   the car through the showroom made nearby cars appear to
                   disappear, so each context now stays spatially stable. */
                garage_transition += dt*1.25f;
                if (garage_transition >= .5f && !garage_transition_swapped) {
                    garage_transition_swapped=1;
                    if (garage_view == GARAGE_TO_CUSTOMIZE) garage_place_workshop();
                    else if (garage_view == GARAGE_TO_DEALERSHIP) {
                        garage_previews_free();
                        (void)load_car(dealer_index);
                        vehicle_reset(&vehicle);
    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
                        vehicle.x=0; vehicle.y=garage_floor_y(&car,1.0f); vehicle.z=0;
                    } else {
                        int slot=profile.active_slot;
                        if (profile.garage_car[slot] < 0) slot=garage_first_occupied();
                        if (slot >= 0 && load_car(profile.garage_car[slot]) == 0) {
                            profile.active_slot=slot;
                            garage_place_active();
                            garage_previews_refresh();
                        }
                    }
                }
                if (garage_transition > 1.0f) {
                    garage_transition=1.0f;
                    if (garage_view == GARAGE_TO_CUSTOMIZE) garage_view=GARAGE_CUSTOMIZE;
                    else if (garage_view == GARAGE_TO_DEALERSHIP) garage_view=GARAGE_DEALERSHIP;
                    else garage_view=GARAGE_FLEET;
                }
            } else if (garage_view == GARAGE_CUSTOMIZE) {
                if (in->pause && garage_detail_open) {
                    garage_paint_preview=-1;
                    garage_paint_preview_rgb=0;
                    garage_detail_open=0;
                    apply_customization(&car, selected);
                } else if (in->pause) {
                    garage_paint_preview=-1;
                    garage_paint_preview_rgb=0;
                    apply_customization(&car, selected);
                    garage_view=GARAGE_TO_FLEET;
                    garage_transition=0.0f; garage_transition_swapped=0;
                } else if (in->start) {
                    /* START launches the built car; A purchases performance. */
                    garage_paint_preview=-1;
                    garage_paint_preview_rgb=0;
                    apply_customization(&car, selected);
                    C3D_FrameSync();
                    garage_previews_free();
                    scene_asset_free(&garage_scene);
                    screen=RACE;
                    engine_audio_set_active(1);
                    race_reset(&race,&vehicle);
                    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
                    vehicle_configure_wheels();
                    race_set_open_world(&race);
                    if (world_stream_init(&palmont,"romfs:/world/palmont/world_index.txt") == 0) {
                        vehicle.x=3747.0f; vehicle.y=169.42f; vehicle.z=-1949.478126f;
                    } else snprintf(message,sizeof(message),"Palmont manifest failed");
                    accumulator=0; safe_position_valid=0;
                    camera_init(&camera,CAM_DRIVE);
                    audio_play("romfs:/audio/countdown.wav");
                } else garage_customize(in);
            } else { /* dealership */
                if (in->pause) {
                    int slot=profile.active_slot;
                    if (profile.garage_car[slot] < 0) slot=garage_first_occupied();
                    if (slot >= 0) {
                        profile.active_slot=slot;
                        garage_view=GARAGE_TO_FLEET;
                        garage_transition=0.0f; garage_transition_swapped=0;
                    }
                } else if (in->next || in->previous) {
                    int step=in->next ? 1 : -1;
                    dealer_index=(dealer_index+car_count+step)%car_count;
                    if (load_car(dealer_index) == 0) {
                        vehicle_reset(&vehicle);
    vehicle_set_performance_levels(&vehicle, profile.performance[selected]);
                        vehicle.x=0; vehicle.y=garage_floor_y(&car,1.0f); vehicle.z=0;
                        audio_play("romfs:/audio/menu.wav");
                    }
                } else if (in->confirm) {
                    unsigned price=dealer_price(dealer_index);
                    if (profile.cash < price) {
                        snprintf(message,sizeof(message),"NEED $%u",price);
                    } else {
                    profile.cash -= price;
                    profile.garage_car[profile.active_slot]=dealer_index;
                    profile_save();
                    garage_view=GARAGE_TO_FLEET;
                    garage_transition=0.0f; garage_transition_swapped=0;
                    audio_play("romfs:/audio/menu.wav");
                    }
                }
            }
        }

        if (screen == GARAGE) {
            garage_motion += dt;
            camera_update_garage(&camera,&vehicle,
                (garage_view == GARAGE_CUSTOMIZE || garage_view == GARAGE_TO_CUSTOMIZE)
                    ? (garage_slot < GARAGE_PERFORMANCE_COUNT ? GARAGE_BODYKIT : GARAGE_VISUAL_CATEGORY(garage_slot)) : GARAGE_BODYKIT,dt);
        } else {
            camera_update(&camera,&vehicle,in,dt);
        }
        return;
    }

    if (in->back) {
        audio_stop_loop_effect();
        audio_stop_skid();
        nitro_loop_playing = skid_loop_playing = 0;
        garage();
        return;
    }

    float world_fx, world_fz;
    vehicle_forward(&vehicle, &world_fx, &world_fz);
    world_stream_update(&palmont, vehicle.x, vehicle.z, world_fx, world_fz);

    if (race.finished) {
        if (in->confirm) {
            race_reset(&race, &vehicle);
            accumulator = 0;
            audio_play("romfs:/audio/countdown.wav");
        }
        return;
    }

    if (in->pause) {
        paused = !paused;
        accumulator = 0;
        audio_pause(paused);
    }
    if (paused)
        return;

    if (in->camera_cycle)
        camera_cycle(&camera);

    if (race.countdown > 0) {
        race.countdown -= dt;
        if (race.countdown <= 0) {
            race.countdown = 0;
            audio_play("romfs:/audio/go.wav");
        }
    } else {
        accumulator += dt;
        while (accumulator >= 1.0f/60.0f) {
            float previous_x = vehicle.x, previous_z = vehicle.z;
            vehicle_update(&vehicle, in, 1.0f/60.0f);
            int road_supported = vehicle_update_road_support(1.0f/60.0f);
            if (!road_supported && safe_position_valid) {
                vehicle.x = safe_x; vehicle.y = safe_y; vehicle.z = safe_z;
                vehicle.speed *= 0.10f;
                vehicle.terrain_pitch *= 0.5f;
            }

            /* The collision proxy contains only vertical features from the
               streamed cell. It is queried after integration, then pushes the
               car out and removes kinetic energy instead of blocking a frame. */
            int wall_resolved = 0;
            float normal_x, normal_z, penetration;
            if (world_stream_wall_contact(&palmont, vehicle.x, vehicle.z, vehicle.y,
                                          0.72f, &normal_x, &normal_z, &penetration)) {
                float into = (vehicle.x-previous_x)*normal_x + (vehicle.z-previous_z)*normal_z;
                /* Resolve the complete overlap, not merely the last movement.
                   This prevents a fast car from remaining inside a wall and
                   leaking through it on following physics steps. */
                vehicle.x += normal_x * (penetration + 0.035f);
                vehicle.z += normal_z * (penetration + 0.035f);
                wall_resolved = 1;
                if (into < 0.0f || penetration > 0.12f) {
                    vehicle.speed *= 0.30f;
                    vehicle.body_roll += normal_x * 0.06f;
                    if (wall_impact_cooldown <= 0.0f) {
                        audio_play("romfs:/audio/impact.wav");
                        wall_impact_cooldown = 0.22f;
                    }
                }
            }
            /* A wall response can move the car into an unsupported cell.
               Re-query only on that exceptional path; normal driving retains
               the first two axle queries as the streaming budget. */
            if (!wall_resolved && road_supported) {
                safe_x = vehicle.x; safe_y = vehicle.y; safe_z = vehicle.z;
                safe_position_valid = 1;
            } else if (wall_resolved) {
                if (vehicle_update_road_support(1.0f/60.0f)) {
                    safe_x = vehicle.x; safe_y = vehicle.y; safe_z = vehicle.z;
                    safe_position_valid = 1;
                } else if (safe_position_valid) {
                    vehicle.x = safe_x; vehicle.y = safe_y; vehicle.z = safe_z;
                    vehicle.speed *= 0.10f;
                }
            }
            if (wall_impact_cooldown > 0.0f)
                wall_impact_cooldown -= 1.0f/60.0f;
            if (race_update(&race, &vehicle, 1.0f/60.0f))
                audio_play("romfs:/audio/impact.wav");
            accumulator -= 1.0f/60.0f;
            if (race.finished) {
                audio_play("romfs:/audio/finish.wav");
                break;
            }
        }
    }

    if (vehicle.nitro_active && !nitro_loop_playing) {
        audio_loop_effect("romfs:/audio/nitro.wav");
        nitro_loop_playing = 1;
    } else if (!vehicle.nitro_active && nitro_loop_playing) {
        audio_stop_loop_effect();
        nitro_loop_playing = 0;
    }
    if (vehicle.skid_active && !skid_loop_playing) {
        audio_loop_skid("romfs:/audio/skid.wav");
        skid_loop_playing = 1;
    } else if (!vehicle.skid_active && skid_loop_playing) {
        audio_stop_skid();
        skid_loop_playing = 0;
    }
    engine_audio_update(vehicle.engine_rpm, vehicle.engine_load);
    camera_update(&camera, &vehicle, in, dt);
}

void game_draw(void)
{
    if (screen == MENU) {
        renderer_draw_main_menu(main_menu_item);
    } else {
        renderer_draw_world(&camera, &car, vehicle.yaw, vehicle.x, vehicle.y, vehicle.z,
                            vehicle.wheel_rotation,
                            screen == GARAGE ? -0.34f : vehicle.steer_visual,
                            screen == GARAGE ? sinf(garage_motion*.82f)*0.020f : vehicle.body_roll,
                            screen == GARAGE ? cosf(garage_motion*.61f)*0.014f : vehicle.body_pitch,
                            screen == RACE ? vehicle.brake_lights : 0,
                            screen == RACE ? vehicle.wheelspin : 0.0f,
                            screen == GARAGE ? &garage_scene : NULL,
                            screen == RACE ? &palmont : NULL, screen == RACE,
                            screen == GARAGE);
    }
    if (screen == GARAGE) {
        /* Four low-memory bodies occupy surrounding bays; the selected car
           remains full-detail in the central position. */
        if (garage_show_fleet()) {
            for (int i=0;i<4;++i) {
                int slot=garage_preview_slot[i];
                if (garage_preview_index[i] >= 0 && slot >= 0)
                    renderer_draw_garage_preview(&camera,&garage_preview[i],garage_bay_yaw[slot],
                        garage_bay_x[slot],garage_floor_y(&garage_preview[i],.72f),
                        garage_bay_z[slot],.72f,i);
            }
        }
        /* Windows are blended only after every car body has populated depth. */
        if (garage_show_fleet()) {
            for (int i=0;i<4;++i) {
                int slot=garage_preview_slot[i];
                if (garage_preview_index[i] >= 0 && slot >= 0)
                    renderer_draw_garage_windows(&camera,&garage_preview[i],garage_bay_yaw[slot],
                        garage_bay_x[slot],garage_floor_y(&garage_preview[i],.72f),garage_bay_z[slot],
                        0,0,.72f,i);
            }
        }
        renderer_draw_garage_windows(&camera,&car,0,vehicle.x,vehicle.y,vehicle.z,
                                     screen == GARAGE ? sinf(garage_motion*.82f)*.020f : vehicle.body_roll,
                                     screen == GARAGE ? cosf(garage_motion*.61f)*.014f : vehicle.body_pitch,
                                     1.0f,-1);
        const char *fleet_names[GARAGE_SLOT_COUNT];
        for (int i=0;i<GARAGE_SLOT_COUNT;i++) {
            int index=profile.garage_car[i];
            fleet_names[i]=(index >= 0 && index < car_count) ? cars[index] : "EMPTY";
        }
        int visual_choice=0, visual_count=1;
        if (garage_slot >= GARAGE_PERFORMANCE_COUNT) {
            int visual=GARAGE_VISUAL_CATEGORY(garage_slot);
            if (visual < 3) {
                visual_choice=vehicle_asset_selected_ordinal(&car, garage_part_slots[visual]);
                visual_count=vehicle_asset_option_count(&car, garage_part_slots[visual]);
            } else if (visual == 3) {
                visual_choice=paint_palette_index(garage_paint_preview_rgb ? garage_paint_preview_rgb : customizations[selected].paint_rgb);
                visual_count=(int)(sizeof(paint_palette_rgb)/sizeof(paint_palette_rgb[0]));
            } else {
                visual_choice=vehicle_asset_wheel_style(&car);
                visual_count=VEHICLE_WHEEL_STYLE_COUNT;
            }
        }
        if (garage_view == GARAGE_TO_CUSTOMIZE || garage_view == GARAGE_TO_FLEET || garage_view == GARAGE_TO_DEALERSHIP)
            renderer_draw_garage_transition(garage_transition,
                garage_view == GARAGE_TO_CUSTOMIZE);
        else if (garage_view == GARAGE_DEALERSHIP)
            renderer_draw_dealer_hud(car.id, dealer_index, car_count, dealer_price(dealer_index), profile.cash);
        else
            renderer_draw_garage_hud(garage_slot, car.id,
                                     garage_view == GARAGE_FLEET, profile.active_slot,
                                     profile.cash, profile.performance[selected], fleet_names, visual_choice, visual_count, garage_detail_open);
    }
    else if (screen == RACE)
        renderer_draw_race_hud(&vehicle, &palmont.roads);

    if (screen == GARAGE && garage_view == GARAGE_CUSTOMIZE && garage_detail_open
        && garage_slot == GARAGE_PAINT) {
        int paint_index=paint_palette_index(garage_paint_preview_rgb ? garage_paint_preview_rgb : customizations[selected].paint_rgb);
        renderer_draw_paint_picker(paint_palette_rgb,
            (int)(sizeof(paint_palette_rgb)/sizeof(paint_palette_rgb[0])), paint_index);
    }

    if (hud_time < 0.1f)
        return;
    hud_time = 0;

    printf("\x1b[2J\x1b[1;1HCarbon Native | EAGL vehicle parts\n");
    printf("%s  %s (%d/%d)\n", game_screen_name(), cars[selected], selected+1, car_count);
    printf("%s\n\n", audio_status());

    if (screen == MENU) {
        printf("D-pad: menu | A: enter selected item (Garage available)\n");
    } else if (screen == GARAGE) {
        if (garage_view == GARAGE_FLEET)
            printf("FLEET slot %d/%d | A: customize  X: dealer  L: remove\n", profile.active_slot+1, GARAGE_SLOT_COUNT);
        else if (garage_view == GARAGE_DEALERSHIP)
            printf("DEALER %s (%d/%d) | left/right browse  A: buy  X: return\n", cars[dealer_index], dealer_index+1, car_count);
        else if (garage_slot < GARAGE_PERFORMANCE_COUNT)
            printf("PERFORMANCE %s level %u/4 | A: install  START: drive  X: fleet\n",
                   garage_performance_names[garage_slot], profile.performance[selected][garage_slot]);
        else
            printf("VISUAL | up/down: section  L/R: part  START: drive  X: fleet\n");

        printf("%s: %lu tri / %lu draws\n", garage_scene.section->section,
               (unsigned long)garage_scene.section->index_count/3,
               (unsigned long)garage_scene.section->batch_count);
        printf("Scene %lu KiB Tex %lu KiB\n",
               (unsigned long)garage_scene.section->geometry_bytes/1024,
               (unsigned long)garage_scene.section->texture_bytes/1024);
        printf("Linear free %lu KiB | %.1f FPS\n",
               (unsigned long)linearSpaceFree()/1024, 1.0f/fmaxf(core_dt(),0.0001f));
        printf("Body kit %d  Hood %d  Spoiler %d\n",
               vehicle_asset_selected_value(&car, VEHICLE_SLOT_BODY),
               vehicle_asset_selected_value(&car, VEHICLE_SLOT_HOOD),
               vehicle_asset_selected_value(&car, VEHICLE_SLOT_SPOILER));
        uint32_t hv=0, hi=0, hg=0, sv=0, si=0, sg=0;
        vehicle_asset_part_counts(&car, VEHICLE_SLOT_HOOD, &hv, &hi, &hg);
        vehicle_asset_part_counts(&car, VEHICLE_SLOT_SPOILER, &sv, &si, &sg);
        printf("H %lu/%lu/%lu  S %lu/%lu/%lu\n",
               (unsigned long)hv, (unsigned long)(hi/3), (unsigned long)hg,
               (unsigned long)sv, (unsigned long)(si/3), (unsigned long)sg);
        printf("Rim %s  Debug %s\n", vehicle_asset_wheel_name(&car),
               renderer_vehicle_debug() ? "ON" : "OFF");
    } else {
        printf("%5.1f km/h  G%d  %4.0f RPM  Nitro %3.0f%%\n",
               vehicle_speed_kmh(&vehicle), vehicle.gear, vehicle.engine_rpm, vehicle.nitro*100);
        printf("Engine %s | roll %.1f pitch %.1f\n", engine_audio_profile(),
               vehicle.body_roll * 57.3f, vehicle.body_pitch * 57.3f);
        printf("Open world free drive\n");
        printf("Palmont %u loaded, %u pending, %.0fm cache / %.0fm fog\n", palmont.loaded,
               palmont.pending, palmont.draw_distance, palmont.render_distance);
        printf("Memory: %lu KiB linear | %lu KiB VRAM free\n",
               (unsigned long)linearSpaceFree()/1024,
               (unsigned long)vramSpaceFree()/1024);
        printf("Time %.2f   Best lap %.2f\n", race.elapsed, race.best_lap);

        if (paused)
            printf("PAUSED - X to resume\n");
        else if (race.countdown > 0)
            printf("START IN %d\n", (int)ceilf(race.countdown));
        else if (race.finished)
            printf("FINISHED! A: retry\n");
        else
            printf("Drive freely through Palmont\n");

        printf("\nA: accelerate B: brake\nR: nitro L: look back\nY: camera X: pause\n");
    }

    printf("\nSELECT: back  |  hold L+R+SELECT: exit\n%s\n", message);
}

void game_shutdown(void)
{
    save_customization(selected);
    profile_save();
    engine_audio_shutdown();
    C3D_FrameSync();
    garage_previews_free();
    scene_asset_free(&garage_scene);
    world_stream_free(&palmont);
    vehicle_asset_free(&car);
}
