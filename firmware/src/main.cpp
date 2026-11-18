#include <Arduino.h>
#include "hw.h"
#include "net.h"
#include "portal.h"
#include "settings.h"
#include "ui.h"

static const uint32_t RESET_HOLD_MS = 5000;   // hold BOOT to erase Wi-Fi setup

void setup() {
    Serial.begin(115200);
    settings_load();
    hw_init();
    ui_init();
    if (!settings_has_wifi()) portal_run();
    net_start();
}

void loop() {
    static uint32_t pressedAt = 0;
    if (hw_boot_pressed()) {
        if (!pressedAt) pressedAt = millis();
        if (millis() - pressedAt > RESET_HOLD_MS) {
            settings_clear_wifi();
            ESP.restart();
        }
    } else pressedAt = 0;
    ui_tick();
    delay(5);
}
