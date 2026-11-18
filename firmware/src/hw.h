#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

extern Arduino_GFX *g;   // 320x240 off-screen canvas, draw here then hw_flush()

void hw_init();
void hw_flush();
void hw_set_brightness(uint8_t level);          // 0-255
bool hw_touch(int &x, int &y);                  // true while a finger is down
bool hw_boot_pressed();
