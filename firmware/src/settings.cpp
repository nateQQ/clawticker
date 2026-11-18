#include "settings.h"
#include <Preferences.h>

Settings cfg;
const uint8_t BRIGHTNESS_LEVELS[4] = {40, 100, 180, 255};
static Preferences prefs;

void settings_load() {
    prefs.begin("clawticker", true);
    cfg.ssid = prefs.getString("ssid", "");
    cfg.pass = prefs.getString("pass", "");
    cfg.host = prefs.getString("host", "");
    cfg.port = prefs.getUShort("port", 8787);
    cfg.brightness = min<uint8_t>(prefs.getUChar("bright", 2), 3);
    prefs.end();
}

void settings_save() {
    prefs.begin("clawticker", false);
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.pass);
    prefs.putString("host", cfg.host);
    prefs.putUShort("port", cfg.port);
    prefs.putUChar("bright", cfg.brightness);
    prefs.end();
}

void settings_clear_wifi() {
    cfg.ssid = cfg.pass = cfg.host = "";
    settings_save();
}

bool settings_has_wifi() { return cfg.ssid.length() > 0 && cfg.host.length() > 0; }

bool settings_load_logo(uint8_t *out) {
    prefs.begin("clawticker", true);
    bool ok = prefs.getBytesLength("logo") == LOGO_BYTES && prefs.getBytes("logo", out, LOGO_BYTES) == LOGO_BYTES;
    prefs.end();
    return ok;
}

void settings_save_logo(const uint8_t *data) {
    prefs.begin("clawticker", false);
    prefs.putBytes("logo", data, LOGO_BYTES);
    prefs.end();
}

void settings_clear_logo() {
    prefs.begin("clawticker", false);
    prefs.remove("logo");
    prefs.end();
}
