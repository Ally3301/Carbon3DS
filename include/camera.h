#pragma once

#include "vehicle.h"
#include "input.h"

/* Names match Player.cpp camera movers in the Zeebo decompile. */
typedef enum CameraMode {
    CAM_DRIVE = 0,  /* DriveCameraMover(Player) */
    CAM_ORBIT = 1,  /* OrbitCarCameraMover */
    CAM_FLYBY = 2   /* FlyByCameraMover */
} CameraMode;

typedef struct Camera {
    CameraMode mode;
    float eye[3];
    float at[3];
    float up[3];
    float orbit_yaw;
    float orbit_pitch;
    float orbit_dist;
} Camera;

void camera_init(Camera *c, CameraMode mode);
void camera_cycle(Camera *c);
void camera_update(Camera *c, const Vehicle *v, const InputState *in, float dt);
/* PocketGarage framing follows the selected upgrade area. */
void camera_update_garage(Camera *c, const Vehicle *v, int focus, float dt);
const char *camera_name(const Camera *c);
