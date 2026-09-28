#include "track.h"
#include <math.h>
void race_reset(Race *r, Vehicle *v)
{
    *r=(Race){.checkpoint=1,.lap=1,.countdown=3};
    vehicle_reset(v); v->x=TRACK_RADIUS;
}
void race_set_open_world(Race *r) { r->open_world=1; }
int race_update(Race *r, Vehicle *v, float dt)
{
    if (r->open_world) { r->elapsed+=dt; r->lap_time+=dt; return 0; }
    if (r->finished || r->countdown>0) return 0;
    r->elapsed+=dt; r->lap_time+=dt; r->impact_cooldown-=dt;
    float radius=hypotf(v->x,v->z);
    float inner=TRACK_RADIUS-TRACK_WIDTH/2+1.2f, outer=TRACK_RADIUS+TRACK_WIDTH/2-1.2f;
    int impact=0;
    if (radius<inner || radius>outer) {
        float target=radius<inner ? inner : outer;
        if (radius<0.001f) { v->x=target; v->z=0; }
        else { v->x*=target/radius; v->z*=target/radius; }
        if (r->impact_cooldown<=0 && v->speed>4) { impact=1; r->impact_cooldown=0.7f; }
        v->speed*=expf(-5.0f*dt);
    }
    float a=r->checkpoint*1.57079632679f;
    float dx=v->x-TRACK_RADIUS*cosf(a), dz=v->z-TRACK_RADIUS*sinf(a);
    /* Ordered quarter gates prevent finish-line oscillation and backwards laps. */
    float tangent=-sinf(a)*sinf(v->yaw)+cosf(a)*cosf(v->yaw);
    if (dx*dx+dz*dz<144 && v->speed>0.5f && tangent>0.15f) {
        r->checkpoint++;
        if (r->checkpoint>4) {
            if (!r->best_lap || r->lap_time<r->best_lap) r->best_lap=r->lap_time;
            r->lap_time=0; r->checkpoint=1;
            if (r->lap==TRACK_LAPS) r->finished=1;
            else r->lap++;
        }
    }
    return impact;
}
