#pragma once

typedef struct InputState {
    float steer;
    float throttle;
    float brake;
    int nitro;
    int look_back;
    int camera_cycle;
    int confirm;
    int back;
    int start;
    int pause;
    int previous, next;
    int slot_previous, slot_next;
    int custom_previous, custom_next;
    int touch_down;
    int touch_x, touch_y;
} InputState;

void input_init(void);
void input_poll(void);
const InputState *input_get(void);
void input_shutdown(void);
