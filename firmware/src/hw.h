#pragma once
#include <Arduino.h>

void hw_init();
void hw_draw(int x, int y, int w, int h, const uint16_t *pixels);   // RGB565 block to the panel
void hw_set_brightness(uint8_t level);                              // 0-255
bool hw_touch(int &x, int &y);                                      // true while a finger is down
bool hw_boot_pressed();
