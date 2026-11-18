#pragma once
#include <Arduino.h>

struct Settings {
    String ssid, pass, host;
    uint16_t port = 8787;
    uint8_t brightness = 2;      // index into BRIGHTNESS_LEVELS
};

extern Settings cfg;
extern const uint8_t BRIGHTNESS_LEVELS[4];

void settings_load();
void settings_save();
void settings_clear_wifi();
bool settings_has_wifi();
