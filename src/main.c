#include "core.h"
#include "resources.h"
#include "renderer.h"
#include "audio.h"
#include "input.h"
#include "game.h"
#include <stdio.h>
static void error_screen(const char *text)
{
    printf("\x1b[2J\x1b[1;1H%s\nhold L + R + SELECT: exit\n",text);
    while (aptMainLoop()) { hidScanInput(); if ((hidKeysHeld() & (KEY_L | KEY_R | KEY_SELECT)) == (KEY_L | KEY_R | KEY_SELECT)) break; gspWaitForVBlank(); }
}
int main(void)
{
    if (core_init()) return 1;
    resources_init();
    if (renderer_init()) { error_screen("GPU initialization failed"); core_shutdown(); return 1; }
    audio_init(); input_init();
    if (game_init()) error_screen("Cannot load vehicle/PocketGarage assets from RomFS");
    else while (core_running()) {
        core_begin_frame();
        input_poll();
        game_update();
        audio_update();
        core_render_begin();
        game_draw();
        core_end_frame();
    }
    game_shutdown(); input_shutdown(); audio_shutdown(); renderer_shutdown();
    resources_shutdown(); core_shutdown();
    return 0;
}
