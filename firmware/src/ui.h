#pragma once
void ui_init();
void ui_tick();           // call every loop(): touch, animation, redraw
void ui_next_screen();    // BOOT button: next content screen (leaves Settings)
void ui_draw_portal();    // static "connect to ClawTicker-Setup" screen
