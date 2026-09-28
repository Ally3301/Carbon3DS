#include "input.h"

#include <3ds.h>

static InputState s_in;

void input_init(void)
{
    s_in = (InputState){0};
}

void input_poll(void)
{
    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();
    circlePosition pad;
    hidCircleRead(&pad);

    float sx = pad.dx / 156.0f;
    float sy = pad.dy / 156.0f;
    if (sx > 1.0f) sx = 1.0f;
    if (sx < -1.0f) sx = -1.0f;
    if (sy > 1.0f) sy = 1.0f;
    if (sy < -1.0f) sy = -1.0f;

    if (sx > -0.12f && sx < 0.12f)
        sx = 0;
    s_in.steer = sx;

    s_in.previous = !!(down & KEY_DLEFT);
    s_in.next = !!(down & KEY_DRIGHT);
    s_in.slot_previous = !!(down & KEY_DUP);
    s_in.slot_next = !!(down & KEY_DDOWN);
    s_in.custom_previous = !!(down & KEY_L);
    s_in.custom_next = !!(down & KEY_R);

    s_in.throttle = 0.0f;
    if (held & KEY_A)
        s_in.throttle = 1.0f;
    if (sy > 0.2f && s_in.throttle < sy)
        s_in.throttle = sy;

    s_in.brake = 0.0f;
    if (held & KEY_B)
        s_in.brake = 1.0f;
    if (sy < -0.2f)
        s_in.brake = -sy;

    s_in.nitro = !!(held & KEY_R);
    s_in.look_back = !!(held & KEY_L);
    s_in.camera_cycle = !!(down & KEY_Y);
    s_in.confirm = !!(down & KEY_A);
    s_in.back = !!(down & KEY_SELECT);
    s_in.start = !!(down & KEY_START);
    touchPosition touch; hidTouchRead(&touch);
    s_in.touch_down=!!(held & KEY_TOUCH);
    s_in.touch_x=touch.px; s_in.touch_y=touch.py;
    s_in.pause = !!(down & KEY_X);
}

const InputState *input_get(void)
{
    return &s_in;
}

void input_shutdown(void)
{
}
