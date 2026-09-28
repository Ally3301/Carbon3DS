#include "camera.h"

#include <math.h>

static float clampf(float v, float lo, float hi)
{ return v < lo ? lo : v > hi ? hi : v; }
static void approach3(float dst[3], const float src[3], float amount)
{ for (int i=0;i<3;++i) dst[i] += (src[i]-dst[i])*amount; }

void camera_init(Camera *c, CameraMode mode)
{
    c->mode = mode;
    c->up[0] = 0.0f; c->up[1] = 1.0f; c->up[2] = 0.0f;
    c->orbit_yaw = 0.72f; c->orbit_pitch = 0.09f; c->orbit_dist = 5.4f;
    c->eye[0] = 0.0f; c->eye[1] = 2.0f; c->eye[2] = -6.0f;
    c->at[0] = 0.0f; c->at[1] = 0.6f; c->at[2] = 0.0f;
}

void camera_cycle(Camera *c) { c->mode = (CameraMode)((c->mode + 1) % 3); }

static void chase(Camera *c, const Vehicle *v, int look_back, float dt)
{
    float fx, fz; vehicle_forward(v, &fx, &fz);
    float speed_ratio=clampf(v->speed/48.0f,0.0f,1.0f);
    float dist=4.9f + v->speed*0.034f;
    float height=1.25f + speed_ratio*0.35f;
    /* Look slightly into the turn at speed, but ease the lateral offset so
       normal lane changes never produce a camera snap. */
    float side = v->steer_visual * speed_ratio * 0.58f;
    if (look_back) { fx=-fx; fz=-fz; dist=5.0f; side=0.0f; }
    float tx[3] = {v->x-fx*dist-fz*side, v->y+height, v->z-fz*dist+fx*side};
    float lead=5.8f+v->speed*0.070f;
    float corner_lead=v->steer_visual*speed_ratio*1.15f;
    float ta[3] = {v->x+fx*lead-fz*corner_lead, v->y+0.50f+speed_ratio*0.15f,
                   v->z+fz*lead+fx*corner_lead};
    /* The camera lags slightly behind acceleration and yaw: more speed is
       visible while the target leads into a corner instead of remaining glued
       to the bumper. */
    float response=clampf(dt*(look_back?14.0f:7.5f),0.0f,1.0f);
    float dx=tx[0]-c->eye[0], dy=tx[1]-c->eye[1], dz=tx[2]-c->eye[2];
    if (dx*dx+dy*dy+dz*dz > 625.0f) {
        /* Race spawn and respawn may be kilometres from the menu origin. */
        for (int i=0;i<3;++i) { c->eye[i]=tx[i]; c->at[i]=ta[i]; }
    } else {
        approach3(c->eye,tx,response);
        approach3(c->at,ta,clampf(dt*9.5f,0.0f,1.0f));
    }
}

void camera_update(Camera *c, const Vehicle *v, const InputState *in, float dt)
{
    if (c->mode == CAM_DRIVE) { chase(c,v,in->look_back,dt); return; }
    if (c->mode == CAM_ORBIT) {
        c->orbit_yaw += in->steer * 1.7f * dt;
        c->orbit_pitch += (in->throttle - in->brake) * 0.6f * dt;
        c->orbit_pitch=clampf(c->orbit_pitch,0.05f,1.2f);
        float cp=cosf(c->orbit_pitch);
        c->eye[0]=v->x+sinf(c->orbit_yaw)*cp*c->orbit_dist;
        c->eye[1]=v->y+1.1f+sinf(c->orbit_pitch)*c->orbit_dist;
        c->eye[2]=v->z+cosf(c->orbit_yaw)*cp*c->orbit_dist;
        c->at[0]=v->x; c->at[1]=v->y+0.55f; c->at[2]=v->z; return;
    }
    float fx,fz; vehicle_forward(v,&fx,&fz);
    c->eye[0]=v->x-fz*5.5f-fx*1.5f; c->eye[1]=v->y+1.2f;
    c->eye[2]=v->z+fx*5.5f-fz*1.5f;
    c->at[0]=v->x+fx*4.0f; c->at[1]=v->y+0.6f; c->at[2]=v->z+fz*4.0f;
}
const char *camera_name(const Camera *c)
{
    switch(c->mode) { case CAM_DRIVE:return "DriveCameraMover(Player)";
    case CAM_ORBIT:return "OrbitCarCameraMover"; case CAM_FLYBY:return "FlyByCameraMover"; }
    return "CameraMover";
}


void camera_update_garage(Camera *c, const Vehicle *v, int focus, float dt)
{
    /* BODY, HOOD, SPOILER, PAINT and RIMS.  The camera is eased instead of
       snapped, matching the inspection-camera behaviour of the Zeebo garage. */
    static const float yaw[]  = {0.72f, 0.10f, 3.02f, 0.72f, 1.57f};
    static const float dist[] = {5.35f, 3.45f, 3.60f, 5.10f, 3.15f};
    static const float lift[] = {1.10f, 0.92f, 0.96f, 1.16f, 0.48f};
    static const float look[] = {0.56f, 0.58f, 0.72f, 0.56f, 0.31f};
    if (focus < 0 || focus >= 5) focus = 0;
    float cp = .985f;
    float target_eye[3] = {
        v->x + sinf(yaw[focus])*cp*dist[focus],
        v->y + lift[focus],
        v->z + cosf(yaw[focus])*cp*dist[focus]
    };
    float target_at[3] = {v->x, v->y+look[focus], v->z};
    float response=clampf(dt*5.8f,0.0f,1.0f);
    approach3(c->eye,target_eye,response);
    approach3(c->at,target_at,clampf(dt*7.2f,0.0f,1.0f));
    c->orbit_yaw=yaw[focus]; c->orbit_dist=dist[focus];
}
