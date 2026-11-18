#pragma once
#include <Arduino.h>

struct Settings {
    String ssid, pass, host;
    uint16_t port = 8787;
    uint8_t brightness = 2;      // index into BRIGHTNESS_LEVELS
    uint8_t display = 255;       // 0 = Claude only, 1 = Claude + Codex, 255 = follow the computer's config
};

extern Settings cfg;
extern const uint8_t BRIGHTNESS_LEVELS[4];

void settings_load();
void settings_save();
void settings_clear_wifi();
bool settings_has_wifi();

// Custom header logo, 32x32 ARGB8888 (B, G, R, A bytes), uploaded from the setup page.
#define LOGO_SIZE 32
#define LOGO_BYTES (LOGO_SIZE * LOGO_SIZE * 4)
bool settings_load_logo(uint8_t *out);          // false if none saved
void settings_save_logo(const uint8_t *data);
void settings_clear_logo();
