#include "net.h"
#include "settings.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static Data latest;
static TaskHandle_t pollTask = nullptr;
static SemaphoreHandle_t lock;
static const uint32_t POLL_MS = 5000;

static void parse(const String &body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) || doc["v"].as<int>() != 1) return;
    Data d;
    d.multi = strcmp(doc["mode"] | "claude", "multi") == 0;
    for (JsonObject c : doc["ai"].as<JsonArray>()) {
        if (d.cardCount >= 2) break;
        Card &k = d.cards[d.cardCount++];
        strlcpy(k.name, c["n"] | "?", sizeof(k.name));
        k.session = constrain(c["s"] | 0, 0, 100);
        k.weekly = constrain(c["w"] | 0, 0, 100);
        k.sessionReset = c["sr"] | -1;
        k.weeklyReset = c["wr"] | -1;
        k.off = (c["off"] | 0) != 0;
    }
    for (JsonObject c : doc["coins"].as<JsonArray>()) {
        if (d.coinCount >= 4) break;
        Coin &k = d.coins[d.coinCount++];
        strlcpy(k.sym, c["s"] | "?", sizeof(k.sym));
        k.price = c["p"] | 0.0;
        k.change24h = c["c"] | 0.0f;
    }
    d.alertId = doc["al"]["i"] | 0;
    strlcpy(d.alertKind, doc["al"]["k"] | "", sizeof(d.alertKind));
    d.localTime = doc["t"] | 0;
    d.receivedAt = millis() ? millis() : 1;
    xSemaphoreTake(lock, portMAX_DELAY);
    latest = d;
    xSemaphoreGive(lock);
}

static uint32_t pollMax = 0;

static void poll_once() {
    uint32_t started = millis();
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(3000);
    String url = "http://" + cfg.host + ":" + String(cfg.port) + "/payload";
    if (cfg.display < 2) url += cfg.display ? "?display=multi" : "?display=claude";
    if (!http.begin(url)) return;
    int code = http.GET();
    if (code == 200) parse(http.getString());
    else Serial.printf("poll %s -> HTTP %d (%s)\n", url.c_str(), code, HTTPClient::errorToString(code).c_str());
    http.end();
    pollMax = max(pollMax, (uint32_t)(millis() - started));
}

static void task(void *) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);   // modem sleep adds latency spikes to every poll
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) poll_once();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_MS));   // sleeps, or wakes early on net_poll_now()
    }
}

void net_start() {
    lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(task, "net", 8192, nullptr, 1, &pollTask, 0);   // UI loop owns core 1
}

bool net_wifi_connected() { return WiFi.status() == WL_CONNECTED; }

bool net_get(Data &out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    out = latest;
    xSemaphoreGive(lock);
    return out.receivedAt != 0;
}

uint32_t net_age_ms() {
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t at = latest.receivedAt;
    xSemaphoreGive(lock);
    return at ? millis() - at : UINT32_MAX;
}

uint32_t net_poll_max_ms() { return pollMax; }

void net_poll_now() { if (pollTask) xTaskNotifyGive(pollTask); }
