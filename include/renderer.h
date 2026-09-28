#pragma once

#include <stdint.h>

#include "camera.h"
#include "scene_asset.h"
#include "vehicle_asset.h"
#include "world_stream.h"

int renderer_init(void);
void renderer_shutdown(void);

void renderer_draw_world(const Camera *cam, const VehicleAsset *vehicle,
                         float yaw, float x, float y, float z,
                         float wheel_rotation, float steer_visual,
                         float body_roll, float body_pitch,
                         int brake_lights, float wheelspin,
                         SceneAsset *scene, const WorldStream *world, int test_world,
                         int defer_vehicle_windows);
void renderer_draw_hud_console(const char *screen_name, const char *camera_name,
                               float speed_kmh, float nitro, const char *hint);
void renderer_draw_garage_hud(int category, const char *car_name, int fleet_mode, int active_slot,
                              unsigned cash, const unsigned char performance[5],
                              const char *const fleet_names[5], int visual_choice, int visual_count, int detail_open);
void renderer_draw_dealer_hud(const char *car_name, int index, int count, unsigned price, unsigned cash);
void renderer_draw_paint_picker(const uint32_t *colors, int count, int selected);
/* Full-screen Carbon-style shutter used while changing garage contexts. */
void renderer_draw_garage_transition(float progress, int to_customize);
void renderer_draw_garage_preview(const Camera *cam, const VehicleAsset *vehicle,
                                  float yaw, float x, float y, float z, float scale,
                                  int preview_id);
void renderer_draw_garage_windows(const Camera *cam, const VehicleAsset *vehicle,
                                  float yaw, float x, float y, float z,
                                  float body_roll, float body_pitch, float scale,
                                  int preview_id);
void renderer_draw_race_hud(const Vehicle *vehicle, const RoadNetwork *roads);
void renderer_draw_main_menu(int selected_item);
void renderer_set_vehicle_debug(int enabled);
int renderer_vehicle_debug(void);
