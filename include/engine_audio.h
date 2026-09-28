#pragma once
void engine_audio_init(void);
void engine_audio_select_vehicle(const char *vehicle_id);
void engine_audio_set_active(int active);
void engine_audio_update(float rpm, float throttle);
void engine_audio_pause(int paused);
void engine_audio_shutdown(void);
const char *engine_audio_profile(void);
