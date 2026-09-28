#pragma once
int audio_init(void);
int audio_available(void);
void audio_play(const char *path);
void audio_music(const char *path);
void audio_loop_effect(const char *path);
void audio_stop_loop_effect(void);
void audio_loop_skid(const char *path);
void audio_stop_skid(void);
void audio_update(void);
void audio_pause(int paused);
const char *audio_status(void);
void audio_shutdown(void);
