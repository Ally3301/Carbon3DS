#pragma once

#include "input.h"

typedef struct Vehicle {
    float x, y, z;
    float yaw;
    float speed;
    float nitro;
    float nitro_cooldown;
    float wheel_rotation;
    float wheel_radius;
    float steer_visual;
    /* Ride target is the minimum body origin that keeps all tyres above the road.
       The spring may extend above it, but never compresses through the ground. */
    float suspension_target;
    float suspension_velocity;
    float suspension_travel;
    float body_roll;
    float body_pitch;
    float terrain_pitch;
    float longitudinal_accel;
    float engine_load;
    int nitro_active;
    int skid_active;
    float wheelspin;
    int brake_lights;
    float engine_rpm;
    int gear;
    /* Engine, turbo, chassis, handling and nitrous levels, 0 through 4. */
    unsigned char performance[5];
} Vehicle;

void vehicle_reset(Vehicle *v);
void vehicle_update(Vehicle *v, const InputState *in, float dt);
float vehicle_speed_kmh(const Vehicle *v);
void vehicle_forward(const Vehicle *v, float *out_x, float *out_z);
void vehicle_set_wheel_radius(Vehicle *v, float radius);
void vehicle_set_performance_levels(Vehicle *v, const unsigned char levels[5]);
void vehicle_suspension_update(Vehicle *v, float support_y, float dt);
