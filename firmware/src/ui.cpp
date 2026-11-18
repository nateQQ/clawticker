#include "ui.h"
#include "data.h"
#include "hw.h"
#include "pins.h"
#include "net.h"
#include "settings.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"
#include <math.h>

#define RGB(r, gr, b) ((uint16_t)((((r) & 0xF8) << 8) | (((gr) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG = 0x0000, C_FG = 0xFFFF;
static const uint16_t C_MUTED = RGB(140, 140, 150), C_TRACK = RGB(42, 42, 48), C_LINE = RGB(60, 60, 66);
static const uint16_t C_ACCENT = RGB(255, 159, 64);
static const uint16_t C_GOOD = RGB(80, 200, 120), C_WARN = RGB(240, 190, 60), C_BAD = RGB(235, 80, 70);

enum Screen { S_AI, S_COIN, S_SPLASH, S_SETTINGS, S_COUNT };
enum Align { LEFT, CENTER, RIGHT };

static Screen screen = S_AI;
static uint32_t lastDraw = 0;
static uint32_t confirmResetUntil = 0;

// ---- drawing helpers ------------------------------------------------------

static void text(const char *s, int x, int top, const GFXfont *font, uint16_t color, Align a = LEFT) {
    g->setFont(font);
    int16_t x1, y1;
    uint16_t w, h;
    g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    int px = a == LEFT ? x : a == CENTER ? x - (int)w / 2 : x - (int)w;
    g->setTextColor(color);
    g->setCursor(px - x1, top - y1);
    g->print(s);
}

static uint16_t severity(int pct) { return pct >= 85 ? C_BAD : pct >= 60 ? C_WARN : C_GOOD; }

static void bar(int x, int y, int w, int h, int pct, uint16_t color) {
    g->fillRoundRect(x, y, w, h, h / 2, C_TRACK);
    int fw = w * pct / 100;
    if (pct > 0) g->fillRoundRect(x, y, max(fw, h), h, h / 2, color);
}

static void fmtReset(char *out, size_t n, int minutes) {
    if (minutes < 0) snprintf(out, n, "--");
    else if (minutes >= 1440) snprintf(out, n, "resets in %dd %dh", minutes / 1440, (minutes % 1440) / 60);
    else if (minutes >= 60) snprintf(out, n, "resets in %dh %dm", minutes / 60, minutes % 60);
    else snprintf(out, n, "resets in %dm", minutes);
}

static void fmtPrice(char *out, size_t n, double p) {
    if (p >= 1000) {
        long v = lround(p);
        if (v >= 1000000) snprintf(out, n, "%ld,%03ld,%03ld", v / 1000000, (v / 1000) % 1000, v % 1000);
        else snprintf(out, n, "%ld,%03ld", v / 1000, v % 1000);
    } else if (p >= 1) snprintf(out, n, "%.2f", p);
    else snprintf(out, n, "%.4f", p);
}

static void header(const char *title, const Data &d, bool have) {
    text(title, 12, 5, &FreeSansBold12pt7b, C_FG);
    bool fresh = have && net_age_ms() < 20000;
    g->fillCircle(SCREEN_W - 14, 15, 4, fresh ? C_GOOD : C_BAD);
    if (have && d.localTime) {
        uint32_t t = d.localTime + (millis() - d.receivedAt) / 1000;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
        text(buf, SCREEN_W - 26, 8, &FreeSans9pt7b, C_MUTED, RIGHT);
    }
    g->drawFastHLine(0, 31, SCREEN_W, C_LINE);
}

static void dots() {
    int x0 = SCREEN_W / 2 - (S_COUNT * 14) / 2 + 5;
    for (int i = 0; i < S_COUNT; i++)
        g->fillCircle(x0 + i * 14, 232, i == screen ? 3 : 2, i == screen ? C_ACCENT : C_LINE);
}

static void waiting(const Data &d, bool have) {
    (void)d;
    text(have ? "Connection lost" : "Waiting for data", SCREEN_W / 2, 90, &FreeSansBold18pt7b, C_FG, CENTER);
    char buf[64];
    if (!net_wifi_connected()) snprintf(buf, sizeof(buf), "Connecting to Wi-Fi %s...", cfg.ssid.c_str());
    else snprintf(buf, sizeof(buf), "Cannot reach %s:%u", cfg.host.c_str(), cfg.port);
    text(buf, SCREEN_W / 2, 135, &FreeSans9pt7b, C_MUTED, CENTER);
    text("Is the daemon running on your computer?", SCREEN_W / 2, 158, &FreeSans9pt7b, C_MUTED, CENTER);
}

// ---- AI usage screen ------------------------------------------------------

static void card_full(const Card &c) {
    char buf[32];
    text("SESSION (5H)", 16, 40, &FreeSans9pt7b, C_MUTED);
    snprintf(buf, sizeof(buf), c.off ? "--" : "%d%%", c.session);
    text(buf, 16, 58, &FreeSansBold24pt7b, c.off ? C_MUTED : severity(c.session));
    fmtReset(buf, sizeof(buf), c.sessionReset);
    text(buf, 304, 76, &FreeSans9pt7b, C_MUTED, RIGHT);
    bar(16, 100, 288, 14, c.off ? 0 : c.session, severity(c.session));
    text("WEEKLY (7D)", 16, 134, &FreeSans9pt7b, C_MUTED);
    snprintf(buf, sizeof(buf), c.off ? "--" : "%d%%", c.weekly);
    text(buf, 16, 152, &FreeSansBold18pt7b, c.off ? C_MUTED : severity(c.weekly));
    fmtReset(buf, sizeof(buf), c.weeklyReset);
    text(buf, 304, 162, &FreeSans9pt7b, C_MUTED, RIGHT);
    bar(16, 190, 288, 12, c.off ? 0 : c.weekly, severity(c.weekly));
}

static void card_half(const Card &c, int x) {
    const int w = 140;
    char buf[32];
    text(c.name, x, 36, &FreeSansBold12pt7b, C_ACCENT);
    text("SESSION", x, 62, &FreeSans9pt7b, C_MUTED);
    snprintf(buf, sizeof(buf), c.off ? "--" : "%d%%", c.session);
    text(buf, x, 78, &FreeSansBold24pt7b, c.off ? C_MUTED : severity(c.session));
    if (c.off) text("not detected", x, 112, &FreeSans9pt7b, C_MUTED);
    else { fmtReset(buf, sizeof(buf), c.sessionReset); text(buf, x, 112, &FreeSans9pt7b, C_MUTED); }
    bar(x, 132, w, 12, c.off ? 0 : c.session, severity(c.session));
    text("WEEKLY", x, 156, &FreeSans9pt7b, C_MUTED);
    snprintf(buf, sizeof(buf), c.off ? "--" : "%d%%", c.weekly);
    text(buf, x, 172, &FreeSansBold18pt7b, c.off ? C_MUTED : severity(c.weekly));
    if (!c.off) { fmtReset(buf, sizeof(buf), c.weeklyReset); text(buf, x, 204, &FreeSans9pt7b, C_MUTED); }
    bar(x, 220 - 6, w, 8, c.off ? 0 : c.weekly, severity(c.weekly));
}

static void draw_ai(const Data &d, bool have) {
    header(have && d.multi ? "AI usage" : "Claude", d, have);
    if (!have || net_age_ms() > 60000 || d.cardCount == 0) { waiting(d, have); return; }
    if (d.multi && d.cardCount >= 2) {
        card_half(d.cards[0], 12);
        g->drawFastVLine(160, 40, 186, C_LINE);
        card_half(d.cards[1], 172);
    } else card_full(d.cards[0]);
}

// ---- Coin screen ----------------------------------------------------------

static void draw_coin(const Data &d, bool have) {
    header("Coin", d, have);
    if (!have || net_age_ms() > 180000 || d.coinCount == 0) {
        text(have ? "No coin data yet" : "Waiting for data", SCREEN_W / 2, 100, &FreeSansBold12pt7b, C_MUTED, CENTER);
        return;
    }
    int rowH = d.coinCount == 1 ? 120 : 46;
    int y = d.coinCount == 1 ? 60 : 40;
    for (int i = 0; i < d.coinCount; i++) {
        const Coin &c = d.coins[i];
        char buf[24];
        bool up = c.change24h >= 0;
        uint16_t col = up ? C_GOOD : C_BAD;
        if (d.coinCount == 1) {
            text(c.sym, 16, y - 20, &FreeSansBold12pt7b, C_ACCENT);
            fmtPrice(buf, sizeof(buf), c.price);
            text("$", 16, y + 10, &FreeSansBold18pt7b, C_MUTED);
            text(buf, 44, y + 6, &FreeSansBold24pt7b, C_FG);
            snprintf(buf, sizeof(buf), "%s%.2f%%  24h", up ? "+" : "", c.change24h);
            text(buf, 16, y + 64, &FreeSansBold12pt7b, col);
        } else {
            text(c.sym, 16, y + 6, &FreeSansBold12pt7b, C_ACCENT);
            fmtPrice(buf, sizeof(buf), c.price);
            text(buf, 150, y + 2, &FreeSansBold12pt7b, C_FG, RIGHT);
            snprintf(buf, sizeof(buf), "%s%.2f%%", up ? "+" : "", c.change24h);
            text(buf, 304, y + 6, &FreeSans9pt7b, col, RIGHT);
            if (i < d.coinCount - 1) g->drawFastHLine(12, y + rowH - 6, SCREEN_W - 24, C_LINE);
        }
        y += rowH;
    }
}

// ---- Splash ---------------------------------------------------------------

static void draw_splash(const Data &d, bool have) {
    int pct = have && d.cardCount ? d.cards[0].session : 0;
    float t = millis() / 1000.0f;
    float speed = pct >= 85 ? 6.0f : pct >= 60 ? 3.0f : 1.5f;
    int bob = lroundf(sinf(t * speed) * 4);
    uint16_t body = have ? severity(pct) : C_MUTED;
    int bx = 112, by = 58 + bob;
    g->fillRoundRect(bx, by, 96, 80, 14, body);
    g->fillRoundRect(bx - 16, by + 26, 16, 22, 6, body);       // arms
    g->fillRoundRect(bx + 96, by + 26, 16, 22, 6, body);
    g->fillRect(bx + 18, by + 80, 14, 12, body);               // legs
    g->fillRect(bx + 64, by + 80, 14, 12, body);
    bool blink = fmodf(t, 3.4f) > 3.25f;
    int eyeH = blink ? 3 : 18;
    g->fillRect(bx + 24, by + 28 + (18 - eyeH) / 2, 12, eyeH, C_BG);
    g->fillRect(bx + 60, by + 28 + (18 - eyeH) / 2, 12, eyeH, C_BG);
    if (pct >= 85) g->fillCircle(bx + 88, by + 10 + (int)(fmodf(t * 20, 14)), 3, RGB(110, 170, 255));
    text("ClawTicker", SCREEN_W / 2, 176, &FreeSansBold12pt7b, C_FG, CENTER);
    char buf[40];
    if (have && d.cardCount && !d.cards[0].off) snprintf(buf, sizeof(buf), "%s %d%% of session", d.cards[0].name, pct);
    else snprintf(buf, sizeof(buf), "Waiting for data");
    text(buf, SCREEN_W / 2, 204, &FreeSans9pt7b, C_MUTED, CENTER);
}

// ---- Settings -------------------------------------------------------------

static const int ROW_Y[4] = {38, 84, 130, 176};
static const int ROW_H = 42;

static void row_label(int i, const char *label) {
    g->drawRect(12, ROW_Y[i], SCREEN_W - 24, ROW_H, C_LINE);
    text(label, 24, ROW_Y[i] + 12, &FreeSansBold9pt7b, C_FG);
}

static void draw_settings(const Data &d, bool have) {
    header("Settings", d, have);
    row_label(0, "Brightness (tap)");
    for (int i = 0; i < 4; i++)
        g->fillRoundRect(190 + i * 28, ROW_Y[0] + 12, 22, 18, 3, i <= cfg.brightness ? C_ACCENT : C_TRACK);
    row_label(1, "Display mode");
    text(have && d.multi ? "Claude + Codex" : "Claude only", 296, ROW_Y[1] + 12, &FreeSans9pt7b, C_MUTED, RIGHT);
    row_label(2, "Computer");
    char buf[48];
    snprintf(buf, sizeof(buf), "%s:%u", cfg.host.c_str(), cfg.port);
    text(buf, 296, ROW_Y[2] + 12, &FreeSans9pt7b, C_MUTED, RIGHT);
    bool confirm = millis() < confirmResetUntil;
    row_label(3, confirm ? "Tap again to erase Wi-Fi setup" : "Reset Wi-Fi setup");
}

static void on_tap_settings(int y) {
    if (y >= ROW_Y[0] && y < ROW_Y[0] + ROW_H) {
        cfg.brightness = (cfg.brightness + 1) % 4;
        hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]);
        settings_save();
    } else if (y >= ROW_Y[3] && y < ROW_Y[3] + ROW_H) {
        if (millis() < confirmResetUntil) {
            settings_clear_wifi();
            ESP.restart();
        }
        confirmResetUntil = millis() + 4000;
    }
}

// ---- main entry points ----------------------------------------------------

void ui_init() {
    hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]);
}

void ui_draw_portal() {
    g->fillScreen(C_BG);
    text("Setup", 12, 5, &FreeSansBold12pt7b, C_FG);
    g->drawFastHLine(0, 31, SCREEN_W, C_LINE);
    text("1. Join Wi-Fi", 16, 50, &FreeSansBold12pt7b, C_ACCENT);
    text("ClawTicker-Setup", 16, 80, &FreeSansBold18pt7b, C_FG);
    text("2. A setup page opens. If not,", 16, 128, &FreeSans9pt7b, C_MUTED);
    text("browse to 192.168.4.1", 16, 150, &FreeSans9pt7b, C_MUTED);
    text("3. Enter Wi-Fi and your computer's IP", 16, 188, &FreeSans9pt7b, C_MUTED);
    hw_flush();
}

void ui_tick() {
    static bool wasDown = false;
    static uint32_t downAt = 0;
    static int lastY = 0;
    int x, y;
    bool down = hw_touch(x, y);
    if (down) lastY = y;
    if (down && !wasDown) downAt = millis();
    if (!down && wasDown && millis() - downAt < 900) {   // tap = release
        if (screen == S_SETTINGS && lastY >= 38) on_tap_settings(lastY);
        else screen = (Screen)((screen + 1) % S_COUNT);
        lastDraw = 0;
    }
    wasDown = down;

    uint32_t now = millis();
    uint32_t interval = screen == S_SPLASH ? 66 : 500;
    if (now - lastDraw < interval) return;
    lastDraw = now;

    Data d;
    bool have = net_get(d);
    g->fillScreen(C_BG);
    switch (screen) {
        case S_AI: draw_ai(d, have); break;
        case S_COIN: draw_coin(d, have); break;
        case S_SPLASH: draw_splash(d, have); break;
        case S_SETTINGS: draw_settings(d, have); break;
        default: break;
    }
    dots();
    hw_flush();
}
