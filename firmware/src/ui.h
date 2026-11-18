#pragma once
void ui_init();
void ui_tick();           // call every loop(): touch, animation, redraw
void ui_next_screen();    // BOOT button: next content screen (leaves Settings)
void ui_draw_portal();    // static "connect to ClawTicker-Setup" screen
void ui_snapshot_serial();   // dump the current screen over serial (dev tool: `shot`)
void ui_toggle_settings();   // same as tapping the gear
void ui_test_alert(const char *kind);   // dev: show the finished-turn screen (`alert claude`)
