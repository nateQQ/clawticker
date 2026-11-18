#include <Arduino.h>
#include <WiFi.h>
#include "hw.h"
#include "net.h"
#include "portal.h"
#include "settings.h"
#include "ui.h"

static const uint32_t RESET_HOLD_MS = 5000;   // hold BOOT to erase Wi-Fi setup
static const uint32_t SHORT_PRESS_MS = 800;   // BOOT short press = next screen

// Serial setup, for when the phone portal is not convenient:
//   status | wifi <ssid>|<password> | host <ip> | port <n> | reset
static uint32_t perfGap = 0, perfTick = 0;   // worst-case timings since the last `perf`

static void handle_command(String line) {
    line.trim();
    if (line == "status") {
        Serial.printf("ssid=%s host=%s:%u wifi=%s ip=%s age=%lums up=%lus reset=%d heap=%u\n", cfg.ssid.c_str(), cfg.host.c_str(),
                      cfg.port, net_wifi_connected() ? "up" : "down", WiFi.localIP().toString().c_str(),
                      (unsigned long)net_age_ms(), (unsigned long)(millis() / 1000), (int)esp_reset_reason(), ESP.getFreeHeap());
    } else if (line.startsWith("wifi ")) {
        String rest = line.substring(5);
        int bar = rest.indexOf('|');
        cfg.ssid = bar < 0 ? rest : rest.substring(0, bar);
        cfg.pass = bar < 0 ? "" : rest.substring(bar + 1);
        settings_save();
        Serial.println("saved, send `reset` to apply");
    } else if (line.startsWith("host ")) {
        cfg.host = line.substring(5);
        cfg.host.trim();
        settings_save();
        Serial.println("saved, send `reset` to apply");
    } else if (line.startsWith("port ")) {
        cfg.port = line.substring(5).toInt();
        settings_save();
        Serial.println("saved, send `reset` to apply");
    } else if (line.startsWith("tap ")) {
        int x = 0, y = 0;
        sscanf(line.c_str() + 4, "%d %d", &x, &y);
        hw_inject_touch(x, y, 120);
    } else if (line == "perf") {
        Serial.printf("loop gap max=%lums  ui_tick max=%lums  poll max=%lums\n", (unsigned long)perfGap,
                      (unsigned long)perfTick, (unsigned long)net_poll_max_ms());
        perfGap = perfTick = 0;
    } else if (line == "portal") {
        portal_run();   // reachable on the board's LAN address too; saving restarts the board
    } else if (line == "shot") {
        ui_snapshot_serial();
    } else if (line == "settings") {
        ui_toggle_settings();
    } else if (line.startsWith("alert")) {
        ui_test_alert(line.length() > 6 ? line.substring(6).c_str() : "claude");
    } else if (line == "next") {
        ui_next_screen();
    } else if (line == "reset") {
        ESP.restart();
    } else if (line.length()) {
        Serial.println("commands: status | wifi <ssid>|<password> | host <ip> | port <n> | reset");
    }
}

static void poll_serial() {
    static String buf;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') { handle_command(buf); buf = ""; }
        else if (buf.length() < 120) buf += c;
    }
}

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
    } else {
        if (pressedAt && millis() - pressedAt < SHORT_PRESS_MS) ui_next_screen();
        pressedAt = 0;
    }
    poll_serial();
    static uint32_t lastLoop = 0;
    uint32_t t0 = millis();
    if (lastLoop) perfGap = max(perfGap, t0 - lastLoop);
    lastLoop = t0;
    ui_tick();
    perfTick = max(perfTick, (uint32_t)(millis() - t0));
    delay(2);
}
