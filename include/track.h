#pragma once
#include "vehicle.h"
#define TRACK_RADIUS 95.0f
#define TRACK_WIDTH 20.0f
#define TRACK_LAPS 3
typedef struct Race { int checkpoint, lap, finished, open_world; float countdown, elapsed, lap_time, best_lap, impact_cooldown; } Race;
void race_reset(Race *r, Vehicle *v);
int race_update(Race *r, Vehicle *v, float dt);
void race_set_open_world(Race *r);
