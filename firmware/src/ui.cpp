#include "ui.h"
#include "data.h"
#include "hw.h"
#include "net.h"
#include "pins.h"
#include "settings.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"
#include <math.h>

#define RGB(r, gr, b) ((uint16_t)((((r) & 0xF8) << 8) | (((gr) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG = 0x0000, C_FG = RGB(250, 249, 245), C_DIM = RGB(176, 174, 165);
static const uint16_t C_TRACK = RGB(38, 38, 38), C_BORDER = RGB(70, 70, 70);
static const uint16_t C_ACCENT = RGB(242, 105, 30);
static const uint16_t C_GOOD = RGB(95, 200, 90), C_WARN = RGB(245, 166, 35), C_BAD = RGB(230, 51, 41);

enum Screen { S_AI, S_COIN, S_SPLASH, S_SETTINGS };
static const int CONTENT_SCREENS = 3;
enum Align { LEFT, CENTER, RIGHT };

static Screen screen = S_AI, prevScreen = S_AI;
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
    g->fillRoundRect(x, y, w, h, 3, C_TRACK);
    if (pct > 0) g->fillRoundRect(x, y, max(w * pct / 100, 6), h, 3, color);
}

static void panel(int x, int y, int w, int h) { g->drawRoundRect(x, y, w, h, 8, C_BORDER); }

// "37m", "1h37m", "4d19h"
static void fmtShort(char *out, size_t n, int minutes) {
    if (minutes < 0) snprintf(out, n, "--");
    else if (minutes >= 1440) snprintf(out, n, "%dd%dh", minutes / 1440, (minutes % 1440) / 60);
    else if (minutes >= 60) snprintf(out, n, "%dh%dm", minutes / 60, minutes % 60);
    else snprintf(out, n, "%dm", minutes);
}

static void fmtPrice(char *out, size_t n, double p) {
    if (p >= 1000) {
        long v = lround(p);
        if (v >= 1000000) snprintf(out, n, "%ld,%03ld,%03ld", v / 1000000, (v / 1000) % 1000, v % 1000);
        else snprintf(out, n, "%ld,%03ld", v / 1000, v % 1000);
    } else if (p >= 1) snprintf(out, n, "%.2f", p);
    else snprintf(out, n, "%.4f", p);
}

// Weekday / month from local seconds (civil-from-days, proleptic Gregorian).
static void fmtDate(char *out, size_t n, uint32_t t) {
    static const char *W[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *M[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    long z = (long)(t / 86400) + 719468;
    long era = z / 146097, doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    int day = doy - (153 * mp + 2) / 5 + 1;
    int mon = mp < 10 ? mp + 3 : mp - 9;
    snprintf(out, n, "%s %d %s", W[((t / 86400) + 4) % 7], day, M[mon - 1]);
}

static void logo(int cx, int cy) {   // small robot-face badge
    g->drawCircle(cx, cy, 15, C_FG);
    g->drawCircle(cx, cy, 14, C_FG);
    g->fillRect(cx - 7, cy - 5, 4, 7, C_FG);
    g->fillRect(cx + 3, cy - 5, 4, 7, C_FG);
    g->drawFastHLine(cx - 5, cy + 7, 10, C_FG);
}

static void header(const char *title, const Data &d, bool have) {
    logo(22, 22);
    text(title, SCREEN_W / 2, 10, &FreeSansBold12pt7b, C_FG, CENTER);
    if (have && d.localTime) {
        uint32_t t = d.localTime + (millis() - d.receivedAt) / 1000;
        char buf[24];
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
        text(buf, SCREEN_W - 10, 4, &FreeSansBold12pt7b, C_FG, RIGHT);
        fmtDate(buf, sizeof(buf), t);
        text(buf, SCREEN_W - 10, 26, &FreeSans9pt7b, C_DIM, RIGHT);
    }
}

static void gear(int cx, int cy, bool close) {
    if (close) {
        g->drawLine(cx - 6, cy - 6, cx + 6, cy + 6, C_DIM);
        g->drawLine(cx - 6, cy + 6, cx + 6, cy - 6, C_DIM);
        g->drawLine(cx - 6, cy - 5, cx + 5, cy + 6, C_DIM);
        g->drawLine(cx - 6, cy + 5, cx + 5, cy - 6, C_DIM);
        return;
    }
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4;
        g->fillCircle(cx + lroundf(cosf(a) * 8), cy + lroundf(sinf(a) * 8), 2, C_DIM);
    }
    g->fillCircle(cx, cy, 6, C_DIM);
    g->fillCircle(cx, cy, 3, C_BG);
}

static void footer(const Data &d, bool have) {
    for (int i = 0; i < CONTENT_SCREENS; i++)
        g->fillCircle(14 + i * 12, 226, 3, (screen != S_SETTINGS && i == (int)screen) ? C_ACCENT : C_BORDER);
    bool fresh = have && net_age_ms() < 20000;
    const char *msg = fresh ? "Connected" : net_wifi_connected() ? "No data" : "Connecting...";
    text(msg, SCREEN_W / 2, 219, &FreeSans9pt7b, fresh ? C_ACCENT : C_DIM, CENTER);
    (void)d;
    gear(SCREEN_W - 20, 226, screen == S_SETTINGS);
}

static void waiting(bool have) {
    text(have ? "Connection lost" : "Waiting for data", SCREEN_W / 2, 84, &FreeSansBold18pt7b, C_FG, CENTER);
    char buf[64];
    if (!net_wifi_connected()) snprintf(buf, sizeof(buf), "Joining Wi-Fi %s", cfg.ssid.c_str());
    else snprintf(buf, sizeof(buf), "Cannot reach %s:%u", cfg.host.c_str(), cfg.port);
    text(buf, SCREEN_W / 2, 126, &FreeSans9pt7b, C_DIM, CENTER);
    text("Is the daemon running on your computer?", SCREEN_W / 2, 148, &FreeSans9pt7b, C_DIM, CENTER);
}

// ---- AI usage screen ------------------------------------------------------

static void usage_block(int x, int y, int w, const char *label, int pct, int resetMin, bool off) {
    char buf[16];
    text(label, x, y, &FreeSans9pt7b, C_DIM);
    snprintf(buf, sizeof(buf), off ? "--" : "%d%%", pct);
    text(buf, x, y + 15, &FreeSansBold18pt7b, off ? C_DIM : severity(pct));
    if (!off) {
        fmtShort(buf, sizeof(buf), resetMin);
        text(buf, x + w, y + 24, &FreeSans9pt7b, C_DIM, RIGHT);
    }
    bar(x, y + 50, w, 10, off ? 0 : pct, severity(pct));
}

static void card(const Card &c, int x, int w) {
    const int y = 48, h = 160;
    panel(x, y, w, h);
    text(c.name, x + 12, y + 8, &FreeSansBold9pt7b, C_FG);
    if (c.off) text("not detected", x + w - 12, y + 9, &FreeSans9pt7b, C_DIM, RIGHT);
    usage_block(x + 12, y + 28, w - 24, "Session", c.session, c.sessionReset, c.off);
    usage_block(x + 12, y + 94, w - 24, "Weekly", c.weekly, c.weeklyReset, c.off);
}

static void draw_ai(const Data &d, bool have) {
    header(have && d.multi ? "AI usage" : "Claude", d, have);
    if (!have || net_age_ms() > 60000 || d.cardCount == 0) { waiting(have); return; }
    if (d.multi && d.cardCount >= 2) {
        card(d.cards[0], 8, 148);
        card(d.cards[1], 164, 148);
    } else card(d.cards[0], 8, 304);
}

// ---- Coin screen ----------------------------------------------------------

static void draw_coin(const Data &d, bool have) {
    header("Coin", d, have);
    if (!have || net_age_ms() > 180000 || d.coinCount == 0) {
        text(have ? "No coin data yet" : "Waiting for data", SCREEN_W / 2, 110, &FreeSansBold12pt7b, C_DIM, CENTER);
        return;
    }
    text("Coin", 12, 52, &FreeSans9pt7b, C_DIM);
    text("Price", 212, 52, &FreeSans9pt7b, C_DIM, RIGHT);
    text("24h", 304, 52, &FreeSans9pt7b, C_DIM, RIGHT);
    int y = 78;
    int rowH = d.coinCount <= 2 ? 52 : 38;
    for (int i = 0; i < d.coinCount; i++) {
        const Coin &c = d.coins[i];
        char buf[24];
        bool up = c.change24h >= 0;
        snprintf(buf, sizeof(buf), "%s/USD", c.sym);
        text(buf, 12, y, &FreeSans9pt7b, C_FG);
        fmtPrice(buf, sizeof(buf), c.price);
        char price[28];
        snprintf(price, sizeof(price), "$%s", buf);
        text(price, 212, y - 2, &FreeSansBold12pt7b, C_FG, RIGHT);
        snprintf(buf, sizeof(buf), "%s%.2f%%", up ? "+" : "", c.change24h);
        text(buf, 304, y, &FreeSans9pt7b, up ? C_GOOD : C_BAD, RIGHT);
        y += rowH;
    }
}

// ---- Splash ---------------------------------------------------------------

static void draw_splash(const Data &d, bool have) {
    int pct = have && d.cardCount ? d.cards[0].session : 0;
    float t = millis() / 1000.0f;
    float speed = pct >= 85 ? 6.0f : pct >= 60 ? 3.0f : 1.5f;
    int bob = lroundf(sinf(t * speed) * 4);
    uint16_t body = have ? severity(pct) : C_DIM;
    int bx = 112, by = 40 + bob;
    g->fillRoundRect(bx, by, 96, 80, 14, body);
    g->fillRoundRect(bx - 16, by + 26, 16, 22, 6, body);
    g->fillRoundRect(bx + 96, by + 26, 16, 22, 6, body);
    g->fillRect(bx + 18, by + 80, 14, 12, body);
    g->fillRect(bx + 64, by + 80, 14, 12, body);
    bool blink = fmodf(t, 3.4f) > 3.25f;
    int eyeH = blink ? 3 : 18;
    g->fillRect(bx + 24, by + 28 + (18 - eyeH) / 2, 12, eyeH, C_BG);
    g->fillRect(bx + 60, by + 28 + (18 - eyeH) / 2, 12, eyeH, C_BG);
    if (pct >= 85) g->fillCircle(bx + 88, by + 10 + (int)(fmodf(t * 20, 14)), 3, RGB(110, 170, 255));
    text("ClawTicker", SCREEN_W / 2, 158, &FreeSansBold12pt7b, C_FG, CENTER);
    char buf[40];
    if (have && d.cardCount && !d.cards[0].off) snprintf(buf, sizeof(buf), "%s %d%% of session", d.cards[0].name, pct);
    else snprintf(buf, sizeof(buf), "Waiting for data");
    text(buf, SCREEN_W / 2, 186, &FreeSans9pt7b, C_DIM, CENTER);
}

// ---- Settings -------------------------------------------------------------

static const int ROW_Y[4] = {50, 90, 130, 170};
static const int ROW_H = 34;

static void row(int i, const char *label, const char *value = nullptr) {
    panel(8, ROW_Y[i], SCREEN_W - 16, ROW_H);
    text(label, 20, ROW_Y[i] + 8, &FreeSansBold9pt7b, C_FG);
    if (value) text(value, SCREEN_W - 20, ROW_Y[i] + 8, &FreeSans9pt7b, C_DIM, RIGHT);
}

static void draw_settings(const Data &d, bool have) {
    header("Settings", d, have);
    row(0, "Brightness");
    for (int i = 0; i < 4; i++)
        g->fillRoundRect(206 + i * 24, ROW_Y[0] + 9, 18, 16, 3, i <= cfg.brightness ? C_ACCENT : C_TRACK);
    row(1, "Display", have && d.multi ? "Claude + Codex" : "Claude only");
    char buf[48];
    snprintf(buf, sizeof(buf), "%s:%u", cfg.host.c_str(), cfg.port);
    row(2, "Computer", buf);
    row(3, millis() < confirmResetUntil ? "Tap again to erase setup" : "Reset Wi-Fi setup");
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

// ---- entry points ---------------------------------------------------------

void ui_init() { hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]); }

void ui_next_screen() {
    screen = screen == S_SETTINGS ? prevScreen : (Screen)((screen + 1) % CONTENT_SCREENS);
    lastDraw = 0;
}

void ui_draw_portal() {
    g->fillScreen(C_BG);
    logo(22, 22);
    text("Setup", SCREEN_W / 2, 10, &FreeSansBold12pt7b, C_FG, CENTER);
    text("1. Join Wi-Fi", 16, 58, &FreeSansBold12pt7b, C_ACCENT);
    text("ClawTicker-Setup", 16, 88, &FreeSansBold18pt7b, C_FG);
    text("2. A setup page opens. If not,", 16, 136, &FreeSans9pt7b, C_DIM);
    text("browse to 192.168.4.1", 16, 158, &FreeSans9pt7b, C_DIM);
    text("3. Enter Wi-Fi and your computer's IP", 16, 196, &FreeSans9pt7b, C_DIM);
    hw_flush();
}

void ui_tick() {
    static bool wasDown = false;
    static uint32_t downAt = 0;
    static int lastX = 0, lastY = 0;
    int x, y;
    bool down = hw_touch(x, y);
    if (down) { lastX = x; lastY = y; }
    if (down && !wasDown) downAt = millis();
    if (!down && wasDown && millis() - downAt < 900) {   // a tap fires on release
        bool onGear = lastX >= SCREEN_W - 52 && lastY >= SCREEN_H - 40;
        if (onGear) {
            if (screen == S_SETTINGS) screen = prevScreen;
            else { prevScreen = screen; screen = S_SETTINGS; }
        } else if (screen == S_SETTINGS) on_tap_settings(lastY);
        else ui_next_screen();
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
    }
    footer(d, have);
    hw_flush();
}
