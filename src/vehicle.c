#include "vehicle.h"

#include <math.h>

/* Lightweight longitudinal/bicycle chassis.  The values are intentionally
 * bounded for the Old 3DS: one scalar speed, no allocation, and only a few
 * spring filters for the body. */
static const float MAX_SPEED = 48.0f;
static const float ENGINE_FORCE = 31.0f;
static const float BRAKE_DECEL = 38.0f;
static const float DRAG = 0.0073f;
static const float ROLLING = 0.72f;
static const float NITRO_FORCE = 15.0f;
static const float NITRO_DRAIN = 0.30f;
static const float NITRO_REGEN = 0.065f;
static const float STEER_BASE = 1.92f;
static const float REVERSE_FORCE = 13.0f;
static const float REVERSE_MAX_SPEED = 11.0f;

static float clampf(float value, float lo, float hi)
{ return value < lo ? lo : value > hi ? hi : value; }

void vehicle_reset(Vehicle *v)
{
    v->x = v->y = v->z = 0.0f;
    v->yaw = 0.0f;
    v->speed = 0.0f;
    v->nitro = 1.0f;
    v->nitro_cooldown = 0.0f;
    v->wheel_rotation = 0.0f;
    v->wheel_radius = 0.36f;
    v->steer_visual = 0.0f;
    v->suspension_target = 0.0f;
    v->suspension_velocity = 0.0f;
    v->suspension_travel = 0.0f;
    v->body_roll = 0.0f;
    v->body_pitch = 0.0f;
    v->terrain_pitch = 0.0f;
    v->longitudinal_accel = 0.0f;
    v->engine_load = 0.0f;
    v->nitro_active = 0;
    v->skid_active = 0;
    v->wheelspin = 0.0f;
    v->brake_lights = 0;
    v->engine_rpm = 900.0f;
    v->gear = 1;
    for (int i=0;i<5;i++) v->performance[i]=0;
}

void vehicle_set_performance_levels(Vehicle *v, const unsigned char levels[5])
{
    if (!v || !levels) return;
    for (int i=0;i<5;i++)
        v->performance[i] = levels[i] > 4 ? 4 : levels[i];
}

void vehicle_forward(const Vehicle *v, float *out_x, float *out_z)
{
    *out_x = sinf(v->yaw);
    *out_z = cosf(v->yaw);
}

void vehicle_update(Vehicle *v, const InputState *in, float dt)
{
    static const float ratio[] = {0.0f, 1.00f, 0.82f, 0.69f, 0.60f, 0.53f};
    static const float shift_floor[] = {0, 0.0f, 8.0f, 16.5f, 25.5f, 35.0f};
    static const float shift_ceil[]  = {0, 12.0f, 21.5f, 31.5f, 41.5f, 51.0f};
    float old_speed = v->speed;
    float forward_speed = v->speed > 0.0f ? v->speed : 0.0f;
    const float engine_bonus = 1.0f + .075f * v->performance[0];
    const float turbo_bonus = 1.0f + .055f * v->performance[1];
    const float chassis_bonus = 1.0f + .060f * v->performance[2];
    const float handling_bonus = 1.0f + .070f * v->performance[3];
    const float nitro_bonus = 1.0f + .090f * v->performance[4];
    const float max_speed = MAX_SPEED * turbo_bonus;
    float normalized_speed = clampf(fabsf(v->speed) / max_speed, 0.0f, 1.0f);
    int boosting = 0;

    if (v->speed > 0.18f || (v->speed >= -0.18f && in->throttle > 0.08f)) {
        if (v->gear < 1) v->gear = 1;
        float q = clampf((forward_speed-shift_floor[v->gear]) /
                         (shift_ceil[v->gear]-shift_floor[v->gear]), 0.0f, 1.0f);
        float predicted_rpm = 1100.0f + 6900.0f*q;
        if (v->gear < 5 && predicted_rpm > 7350.0f) v->gear++;
        else if (v->gear > 1 && predicted_rpm < 2100.0f) v->gear--;
        q = clampf((forward_speed-shift_floor[v->gear]) /
                   (shift_ceil[v->gear]-shift_floor[v->gear]), 0.0f, 1.0f);
        float torque_curve = 0.62f + 0.38f*sinf(q*1.57079632679f);
        boosting = in->nitro && v->nitro > 0.03f && in->throttle > 0.08f;
        float drive = ENGINE_FORCE*engine_bonus*ratio[v->gear]*torque_curve*in->throttle +
                      (boosting ? NITRO_FORCE*nitro_bonus : 0.0f);
        float resistance = ROLLING + DRAG*v->speed*v->speed;
        v->speed += (drive - BRAKE_DECEL*chassis_bonus*in->brake - resistance)*dt;
        if (v->speed < 0.0f) v->speed = 0.0f;
        v->speed = clampf(v->speed, 0.0f, max_speed+(boosting ? 6.0f*nitro_bonus : 0.0f));
        float target_rpm = 1100.0f+6900.0f*q;
        if (v->speed < 1.0f) target_rpm = 900.0f+1700.0f*in->throttle;
        float response = target_rpm < v->engine_rpm ? 15.0f : 8.5f;
        v->engine_rpm += (target_rpm-v->engine_rpm)*clampf(dt*response,0.0f,1.0f);
        v->engine_load = clampf(in->throttle+(boosting ? .18f:0.0f),0.0f,1.0f);
    } else {
        /* Holding brake after stopping selects R; throttle always brakes R
           back to neutral/forward, matching an automatic street car. */
        v->gear = -1;
        if (in->throttle > 0.08f) {
            v->speed += BRAKE_DECEL*in->throttle*dt;
            if (v->speed > 0.0f) v->speed = 0.0f;
        } else {
            float resistance = ROLLING + DRAG*v->speed*v->speed;
            v->speed -= (REVERSE_FORCE*in->brake - resistance)*dt;
            if (v->speed < -REVERSE_MAX_SPEED) v->speed = -REVERSE_MAX_SPEED;
        }
        float target_rpm = 900.0f + 2600.0f*clampf(fabsf(v->speed)/REVERSE_MAX_SPEED,0,1)*in->brake;
        v->engine_rpm += (target_rpm-v->engine_rpm)*clampf(dt*10.0f,0,1);
        v->engine_load = in->brake;
    }

    v->longitudinal_accel = (v->speed-old_speed)/dt;
    float steering_grip = (1.0f-0.48f*normalized_speed) * handling_bonus;
    if (boosting) steering_grip *= .88f;
    if (steering_grip < .38f) steering_grip=.38f;
    if (fabsf(v->speed) > .25f)
        v->yaw -= in->steer*STEER_BASE*steering_grip*(v->speed >= 0 ? 1.0f : -1.0f)*dt;

    float fx,fz; vehicle_forward(v,&fx,&fz);
    v->x += fx*v->speed*dt; v->z += fz*v->speed*dt;
    v->wheel_rotation += v->speed*dt/v->wheel_radius;
    if (fabsf(v->wheel_rotation)>6.28318530718f)
        v->wheel_rotation=fmodf(v->wheel_rotation,6.28318530718f);
    v->steer_visual += (in->steer-v->steer_visual)*clampf(dt*11.0f,0,1);

    float launch = in->throttle*clampf((8.0f-fabsf(v->speed))/8.0f,0,1);
    float corner = fabsf(in->steer)*clampf(fabsf(v->speed)/14.0f,0,1);
    float spin_target = fmaxf(launch*.72f, corner*.55f);
    v->wheelspin += (spin_target-v->wheelspin)*clampf(dt*8.0f,0,1);
    v->skid_active = v->wheelspin > .34f;
    v->brake_lights = in->brake > 0.08f && v->speed > 0.4f;

    float roll_target=in->steer*normalized_speed*normalized_speed*.085f;
    float pitch_target=v->terrain_pitch+clampf(-v->longitudinal_accel*.0022f,-.030f,.030f);
    v->body_roll += (roll_target-v->body_roll)*clampf(dt*7.5f,0,1);
    v->body_pitch += (pitch_target-v->body_pitch)*clampf(dt*7.5f,0,1);
    v->nitro_active=boosting;
    if (boosting) {
        v->nitro-=NITRO_DRAIN*dt/(1.0f+.12f*v->performance[4]);
        if (v->nitro <= 0.0f) {
            v->nitro=0.0f;
            v->nitro_active=0;
            v->nitro_cooldown=0.85f;
        }
    } else if (v->nitro_cooldown > 0.0f) {
        v->nitro_cooldown=fmaxf(0.0f,v->nitro_cooldown-dt);
    } else {
        v->nitro+=NITRO_REGEN*(1.0f+.04f*v->performance[4])*dt;
    }
    v->nitro=clampf(v->nitro,0,1);
}

float vehicle_speed_kmh(const Vehicle *v) { return fabsf(v->speed) * 3.6f; }

void vehicle_set_wheel_radius(Vehicle *v, float radius)
{
    if (!v) return;
    v->wheel_radius = clampf(radius, 0.12f, 0.70f);
}

void vehicle_suspension_update(Vehicle *v, float support_y, float dt)
{
    if (!v || dt <= 0.0f) return;
    v->suspension_target = support_y;
    if (support_y >= v->y) {
        v->y = support_y;
        v->suspension_velocity = 0.0f;
    } else {
        float error = support_y - v->y;
        v->suspension_velocity += error * 92.0f * dt;
        v->suspension_velocity /= 1.0f + 14.0f * dt;
        v->y += v->suspension_velocity * dt;
        if (v->y < support_y) {
            v->y = support_y;
            v->suspension_velocity = 0.0f;
        }
    }
    v->suspension_travel = v->y - support_y;
}
