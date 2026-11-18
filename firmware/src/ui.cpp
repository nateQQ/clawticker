#include "ui.h"
#include <lvgl.h>
#include <math.h>
#include "data.h"
#include "hw.h"
#include "net.h"
#include "pins.h"
#include "settings.h"

LV_FONT_DECLARE(inter_12);
LV_FONT_DECLARE(inter_14);
LV_FONT_DECLARE(inter_16);
LV_FONT_DECLARE(inter_18);
LV_FONT_DECLARE(inter_22);
LV_FONT_DECLARE(inter_32);

#define COL(hex) lv_color_hex(hex)
static const uint32_t K_BG = 0x000000, K_FG = 0xFAF9F5, K_DIM = 0xB0AEA5, K_TRACK = 0x262626;
static const uint32_t K_BORDER = 0x505050, K_ACCENT = 0xF2691E, K_ACCENT_DIM = 0x8F4423;
static const uint32_t K_GOOD = 0x5FC85A, K_BAD = 0xE63329, K_BAD_DIM = 0x8C2A24;

enum Screen { S_AI, S_COIN, S_SPLASH, S_SETTINGS, S_COUNT };
static const int CONTENT_SCREENS = 3;
static const int SESSION_MIN = 300, WEEK_MIN = 10080;   // window lengths, for the pace marker

static lv_obj_t *scr[S_COUNT];
static Screen current = S_AI, previous = S_AI;
static uint32_t confirmResetUntil = 0;

// ---- small builders -------------------------------------------------------

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius = 0) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, COL(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *txt = "") {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

static void set_text(lv_obj_t *l, const char *txt) {
    if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

static void set_color(lv_obj_t *o, uint32_t color) { lv_obj_set_style_text_color(o, COL(color), 0); }

// "37m", "1h37m", "4d19h"
static void fmt_short(char *out, size_t n, int minutes) {
    if (minutes < 0) snprintf(out, n, "--");
    else if (minutes >= 1440) snprintf(out, n, "%dd%dh", minutes / 1440, (minutes % 1440) / 60);
    else if (minutes >= 60) snprintf(out, n, "%dh%dm", minutes / 60, minutes % 60);
    else snprintf(out, n, "%dm", minutes);
}

static void fmt_price(char *out, size_t n, double p) {
    if (p >= 1000) {
        long v = lround(p);
        if (v >= 1000000) snprintf(out, n, "%ld,%03ld,%03ld", v / 1000000, (v / 1000) % 1000, v % 1000);
        else snprintf(out, n, "%ld,%03ld", v / 1000, v % 1000);
    } else if (p >= 1) snprintf(out, n, "%.2f", p);
    else snprintf(out, n, "%.4f", p);
}

// Weekday / day / month from local seconds (civil-from-days).
static void fmt_date(char *out, size_t n, uint32_t t) {
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

// ---- chrome shared by every screen (header + footer) ----------------------

struct Chrome {
    lv_obj_t *title, *clock, *date, *status, *gear, *dots[CONTENT_SCREENS];
};
static Chrome chrome[S_COUNT];

static void on_screen_tap(lv_event_t *) {
    if (current == S_SETTINGS) return;
    ui_next_screen();
}

static void show(Screen s) {
    current = s;
    lv_screen_load(scr[s]);
}

static void on_gear(lv_event_t *) {
    if (current == S_SETTINGS) show(previous);
    else { previous = current; show(S_SETTINGS); }
}

static lv_obj_t *make_screen(Screen id, const char *title) {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s, COL(K_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, on_screen_tap, LV_EVENT_CLICKED, nullptr);
    Chrome &c = chrome[id];

    // logo: ring with two eyes and a mouth
    lv_obj_t *ring = box(s, 6, 6, 32, 32, K_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(ring, COL(K_FG), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    box(ring, 8, 8, 4, 8, K_FG, 1);
    box(ring, 16, 8, 4, 8, K_FG, 1);
    box(ring, 8, 19, 12, 2, K_FG, 1);

    c.title = label(s, &inter_18, K_FG, title);
    lv_obj_align(c.title, LV_ALIGN_TOP_MID, 0, 12);
    c.clock = label(s, &inter_22, K_FG);
    lv_obj_align(c.clock, LV_ALIGN_TOP_RIGHT, -10, 4);
    c.date = label(s, &inter_12, K_DIM);
    lv_obj_align(c.date, LV_ALIGN_TOP_RIGHT, -10, 29);

    for (int i = 0; i < CONTENT_SCREENS; i++) {
        c.dots[i] = box(s, 12 + i * 12, 223, 6, 6, K_BORDER, LV_RADIUS_CIRCLE);
    }
    c.status = label(s, &inter_14, K_DIM);
    lv_obj_align(c.status, LV_ALIGN_BOTTOM_MID, 0, -6);
    c.gear = label(s, &lv_font_montserrat_14, K_DIM, id == S_SETTINGS ? LV_SYMBOL_CLOSE : LV_SYMBOL_SETTINGS);
    lv_obj_align(c.gear, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_add_flag(c.gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c.gear, 22);
    lv_obj_add_event_cb(c.gear, on_gear, LV_EVENT_CLICKED, nullptr);
    return s;
}

static void refresh_chrome(Screen id, const Data &d, bool have) {
    Chrome &c = chrome[id];
    if (have && d.localTime) {
        uint32_t t = d.localTime + (millis() - d.receivedAt) / 1000;
        char buf[24];
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
        set_text(c.clock, buf);
        fmt_date(buf, sizeof(buf), t);
        set_text(c.date, buf);
    }
    bool fresh = have && net_age_ms() < 20000;
    set_text(c.status, fresh ? "Connected" : net_wifi_connected() ? "No data" : "Connecting...");
    set_color(c.status, fresh ? K_ACCENT : K_DIM);
    lv_obj_align(c.status, LV_ALIGN_BOTTOM_MID, 0, -6);
    for (int i = 0; i < CONTENT_SCREENS; i++)
        lv_obj_set_style_bg_color(c.dots[i], COL(id != S_SETTINGS && i == (int)id ? K_ACCENT : K_BORDER), 0);
}

// ---- AI usage screen ------------------------------------------------------

struct Meter {
    lv_obj_t *pct, *reset, *track, *fill, *mark;
};
struct CardUI {
    lv_obj_t *root, *name, *off;
    Meter session, weekly;
};
static CardUI cards[2];
static const int CARD_Y = 48, CARD_H = 158, BAR_H = 12;

static Meter make_meter(lv_obj_t *card, int y) {
    Meter m;
    m.pct = label(card, &inter_18, K_FG, "--");
    lv_obj_set_pos(m.pct, 12, y);
    m.reset = label(card, &inter_14, K_DIM);
    m.track = box(card, 12, y + 31, 100, BAR_H, K_TRACK, 3);
    m.fill = box(m.track, 0, 0, 0, BAR_H, K_ACCENT, 3);
    m.mark = box(card, 0, y + 28, 2, BAR_H + 6, K_FG);
    return m;
}

static void make_card(CardUI &c, lv_obj_t *parent) {
    c.root = lv_obj_create(parent);
    lv_obj_remove_style_all(c.root);
    lv_obj_set_pos(c.root, 8, CARD_Y);
    lv_obj_set_style_border_color(c.root, COL(K_BORDER), 0);
    lv_obj_set_style_border_width(c.root, 1, 0);
    lv_obj_set_style_radius(c.root, 10, 0);
    c.name = label(c.root, &inter_14, K_DIM);
    lv_obj_set_pos(c.name, 12, 8);
    c.off = label(c.root, &inter_12, K_DIM, "not detected");
    c.session = make_meter(c.root, 34);
    c.weekly = make_meter(c.root, 100);
}

static void layout_card(CardUI &c, int x, int w) {
    lv_obj_set_pos(c.root, x, CARD_Y);
    lv_obj_set_size(c.root, w, CARD_H);
    lv_obj_align(c.off, LV_ALIGN_TOP_RIGHT, -12, 10);
    Meter *ms[2] = {&c.session, &c.weekly};
    for (Meter *m : ms) {
        lv_obj_set_width(m->track, w - 26);
        lv_obj_align_to(m->reset, m->pct, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
        lv_obj_set_x(m->reset, w - 14 - lv_obj_get_width(m->reset));
    }
}

static void update_meter(Meter &m, int pct, int resetMin, int windowMin, bool off, bool weekly, int cardW) {
    char buf[16];
    int trackW = cardW - 26;
    snprintf(buf, sizeof(buf), off ? "--" : "%d%%", pct);
    set_text(m.pct, buf);
    set_color(m.pct, weekly && !off ? K_DIM : K_FG);
    fmt_short(buf, sizeof(buf), off ? -1 : resetMin);
    set_text(m.reset, off ? "" : buf);
    lv_obj_set_x(m.reset, cardW - 14 - lv_obj_get_width(m.reset));
    bool hot = pct >= 85;
    lv_obj_set_style_bg_color(m.fill, COL(hot ? (weekly ? K_BAD_DIM : K_BAD) : (weekly ? K_ACCENT_DIM : K_ACCENT)), 0);
    lv_obj_set_width(m.fill, off ? 0 : max(pct * trackW / 100, pct > 0 ? 6 : 0));
    if (off || resetMin < 0) lv_obj_add_flag(m.mark, LV_OBJ_FLAG_HIDDEN);
    else {
        // pace marker: how far through the window we are right now
        float elapsed = 1.0f - (float)min(resetMin, windowMin) / windowMin;
        lv_obj_remove_flag(m.mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(m.mark, 12 + (int)(elapsed * (trackW - 2)));
    }
}

static lv_obj_t *waiting_box, *waiting_title, *waiting_hint;

static void build_ai() {
    lv_obj_t *s = scr[S_AI];
    make_card(cards[0], s);
    make_card(cards[1], s);
    waiting_box = box(s, 0, 44, SCREEN_W, 160, K_BG);
    lv_obj_remove_flag(waiting_box, LV_OBJ_FLAG_CLICKABLE);
    waiting_title = label(waiting_box, &inter_22, K_FG, "Waiting for data");
    lv_obj_align(waiting_title, LV_ALIGN_TOP_MID, 0, 40);
    waiting_hint = label(waiting_box, &inter_14, K_DIM);
    lv_obj_set_style_text_align(waiting_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(waiting_hint, LV_ALIGN_TOP_MID, 0, 80);
}

static void update_ai(const Data &d, bool have) {
    bool live = have && net_age_ms() < 60000 && d.cardCount > 0;
    if (live) lv_obj_add_flag(waiting_box, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(waiting_box, LV_OBJ_FLAG_HIDDEN);
        char buf[96];
        if (!net_wifi_connected()) snprintf(buf, sizeof(buf), "Joining Wi-Fi %s", cfg.ssid.c_str());
        else snprintf(buf, sizeof(buf), "Cannot reach %s:%u\nIs the daemon running on your computer?", cfg.host.c_str(), cfg.port);
        set_text(waiting_title, have ? "Connection lost" : "Waiting for data");
        lv_obj_align(waiting_title, LV_ALIGN_TOP_MID, 0, 40);
        set_text(waiting_hint, buf);
        lv_obj_align(waiting_hint, LV_ALIGN_TOP_MID, 0, 80);
    }
    bool multi = live && d.multi && d.cardCount >= 2;
    set_text(chrome[S_AI].title, multi ? "AI usage" : "Claude");
    lv_obj_align(chrome[S_AI].title, LV_ALIGN_TOP_MID, 0, 12);
    for (int i = 0; i < 2; i++) {
        bool shown = live && (i == 0 || multi);
        if (!shown) { lv_obj_add_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN);
        int w = multi ? 148 : 304, x = multi ? (i == 0 ? 8 : 164) : 8;
        layout_card(cards[i], x, w);
        const Card &c = d.cards[i];
        set_text(cards[i].name, multi ? c.name : "Plan usage");
        if (c.off) lv_obj_remove_flag(cards[i].off, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cards[i].off, LV_OBJ_FLAG_HIDDEN);
        update_meter(cards[i].session, c.session, c.sessionReset, SESSION_MIN, c.off, false, w);
        update_meter(cards[i].weekly, c.weekly, c.weeklyReset, WEEK_MIN, c.off, true, w);
    }
}

// ---- Coin screen ----------------------------------------------------------

struct CoinRow { lv_obj_t *pair, *price, *change; };
static CoinRow coinRows[4];
static lv_obj_t *coin_empty;

static void build_coin() {
    lv_obj_t *s = scr[S_COIN];
    lv_obj_t *h1 = label(s, &inter_14, K_DIM, "Coin");
    lv_obj_set_pos(h1, 12, 52);
    lv_obj_t *h2 = label(s, &inter_14, K_DIM, "Price");
    lv_obj_align(h2, LV_ALIGN_TOP_LEFT, 150, 52);
    lv_obj_t *h3 = label(s, &inter_14, K_DIM, "24h");
    lv_obj_align(h3, LV_ALIGN_TOP_RIGHT, -12, 52);
    for (int i = 0; i < 4; i++) {
        coinRows[i].pair = label(s, &inter_16, K_FG);
        coinRows[i].price = label(s, &inter_18, K_FG);
        coinRows[i].change = label(s, &inter_14, K_GOOD);
    }
    coin_empty = label(s, &inter_18, K_DIM, "Waiting for data");
    lv_obj_align(coin_empty, LV_ALIGN_CENTER, 0, -6);
}

static void update_coin(const Data &d, bool have) {
    bool live = have && net_age_ms() < 180000 && d.coinCount > 0;
    if (live) lv_obj_add_flag(coin_empty, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(coin_empty, LV_OBJ_FLAG_HIDDEN);
        set_text(coin_empty, have ? "No coin data yet" : "Waiting for data");
        lv_obj_align(coin_empty, LV_ALIGN_CENTER, 0, -6);
    }
    int rowH = d.coinCount <= 2 ? 56 : 38;
    for (int i = 0; i < 4; i++) {
        CoinRow &r = coinRows[i];
        if (!live || i >= d.coinCount) {
            lv_obj_add_flag(r.pair, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.price, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.change, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(r.pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r.price, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r.change, LV_OBJ_FLAG_HIDDEN);
        const Coin &c = d.coins[i];
        char buf[32], num[24];
        int y = 84 + i * rowH;
        snprintf(buf, sizeof(buf), "%s/USD", c.sym);
        set_text(r.pair, buf);
        lv_obj_set_pos(r.pair, 12, y + 2);
        fmt_price(num, sizeof(num), c.price);
        snprintf(buf, sizeof(buf), "$%s", num);
        set_text(r.price, buf);
        lv_obj_align(r.price, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_pos(r.price, 212 - lv_obj_get_width(r.price), y - 1);
        snprintf(buf, sizeof(buf), "%s%.2f%%", c.change24h >= 0 ? "+" : "", c.change24h);
        set_text(r.change, buf);
        set_color(r.change, c.change24h >= 0 ? K_GOOD : K_BAD);
        lv_obj_set_pos(r.change, 308 - lv_obj_get_width(r.change), y + 2);
    }
}

// ---- Splash ---------------------------------------------------------------

static lv_obj_t *mascot, *eyeL, *eyeR, *drop, *splash_sub;

static void build_splash() {
    lv_obj_t *s = scr[S_SPLASH];
    mascot = box(s, 112, 44, 96, 80, K_DIM, 14);
    box(mascot, -16, 26, 16, 22, K_DIM, 6);   // arms
    box(mascot, 96, 26, 16, 22, K_DIM, 6);
    box(mascot, 18, 80, 14, 12, K_DIM);
    box(mascot, 64, 80, 14, 12, K_DIM);
    eyeL = box(mascot, 24, 28, 12, 18, K_BG, 2);
    eyeR = box(mascot, 60, 28, 12, 18, K_BG, 2);
    drop = box(mascot, 84, 6, 6, 6, 0x6EAAFF, LV_RADIUS_CIRCLE);
    lv_obj_t *name = label(s, &inter_22, K_FG, "ClawTicker");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 156);
    splash_sub = label(s, &inter_14, K_DIM, "Waiting for data");
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 186);
}

static void update_splash(const Data &d, bool have) {
    int pct = have && d.cardCount && !d.cards[0].off ? d.cards[0].session : 0;
    float t = millis() / 1000.0f;
    float speed = pct >= 85 ? 6.0f : pct >= 60 ? 3.0f : 1.5f;
    lv_obj_set_y(mascot, 44 + lroundf(sinf(t * speed) * 4));
    uint32_t body = !have ? K_DIM : pct >= 85 ? K_BAD : pct >= 60 ? K_ACCENT : K_GOOD;
    // all body parts are the mascot's children plus the mascot itself
    lv_obj_set_style_bg_color(mascot, COL(body), 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(mascot); i++) {
        lv_obj_t *ch = lv_obj_get_child(mascot, i);
        if (ch == eyeL || ch == eyeR || ch == drop) continue;
        lv_obj_set_style_bg_color(ch, COL(body), 0);
    }
    bool blink = fmodf(t, 3.4f) > 3.25f;
    for (lv_obj_t *e : {eyeL, eyeR}) {
        lv_obj_set_height(e, blink ? 3 : 18);
        lv_obj_set_y(e, blink ? 36 : 28);
    }
    if (pct >= 85) { lv_obj_remove_flag(drop, LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(drop, 6 + (int)fmodf(t * 20, 14)); }
    else lv_obj_add_flag(drop, LV_OBJ_FLAG_HIDDEN);
    char buf[40];
    if (have && d.cardCount && !d.cards[0].off) snprintf(buf, sizeof(buf), "%s %d%% of session", d.cards[0].name, pct);
    else snprintf(buf, sizeof(buf), "Waiting for data");
    set_text(splash_sub, buf);
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 186);
}

// ---- Settings -------------------------------------------------------------

static lv_obj_t *bright_val, *display_val, *host_val, *reset_lbl;

static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, const char *txt, lv_obj_t **out) {
    lv_obj_t *p = box(parent, x, y, w, 30, K_TRACK, LV_RADIUS_CIRCLE);
    lv_obj_t *l = label(p, &inter_14, K_FG, txt);
    lv_obj_center(l);
    if (out) *out = l;
    return p;
}

static void on_bright(lv_event_t *e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    cfg.brightness = constrain((int)cfg.brightness + delta, 0, 3);
    hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]);
    settings_save();
}

static void on_reset(lv_event_t *) {
    if (millis() < confirmResetUntil) {
        settings_clear_wifi();
        ESP.restart();
    }
    confirmResetUntil = millis() + 4000;
}

static void setting_row(lv_obj_t *s, int y, const char *name) {
    lv_obj_t *l = label(s, &inter_16, K_FG, name);
    lv_obj_set_pos(l, 14, y + 5);
}

static void build_settings() {
    lv_obj_t *s = scr[S_SETTINGS];
    setting_row(s, 50, "Brightness");
    lv_obj_t *minus = pill(s, 196, 50, 34, "-", nullptr);
    lv_obj_t *plus = pill(s, 274, 50, 34, "+", nullptr);
    lv_obj_t *mid = label(s, &inter_14, K_FG, "");
    bright_val = mid;
    lv_obj_set_pos(mid, 238, 57);
    for (lv_obj_t *b : {minus, plus}) lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(minus, on_bright, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(plus, on_bright, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    setting_row(s, 92, "Display");
    pill(s, 170, 92, 138, "", &display_val);
    setting_row(s, 134, "Computer");
    pill(s, 150, 134, 158, "", &host_val);
    setting_row(s, 176, "Wi-Fi");
    lv_obj_t *rp = pill(s, 178, 176, 130, "Reset setup", &reset_lbl);
    lv_obj_add_flag(rp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rp, on_reset, LV_EVENT_CLICKED, nullptr);
}

static void update_settings(const Data &d, bool have) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%d/4", cfg.brightness + 1);
    set_text(bright_val, buf);
    lv_obj_set_pos(bright_val, 252 - lv_obj_get_width(bright_val) / 2, 57);
    set_text(display_val, have && d.multi ? "Claude + Codex" : "Claude only");
    lv_obj_center(display_val);
    snprintf(buf, sizeof(buf), "%s:%u", cfg.host.c_str(), cfg.port);
    set_text(host_val, buf);
    lv_obj_center(host_val);
    set_text(reset_lbl, millis() < confirmResetUntil ? "Tap again" : "Reset setup");
    lv_obj_center(reset_lbl);
}

// ---- LVGL glue ------------------------------------------------------------

static lv_display_t *disp;
static uint8_t *drawBuf[2];

static void flush_cb(lv_display_t *display, const lv_area_t *a, uint8_t *px) {
    hw_draw(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, (const uint16_t *)px);
    lv_display_flush_ready(display);
}

static void touch_cb(lv_indev_t *, lv_indev_data_t *data) {
    int x, y;
    if (hw_touch(x, y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else data->state = LV_INDEV_STATE_RELEASED;
}

void ui_init() {
    hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]);
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    const size_t bufBytes = SCREEN_W * 40 * sizeof(uint16_t);
    drawBuf[0] = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL);
    drawBuf[1] = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL);
    disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, drawBuf[0], drawBuf[1], bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);

    scr[S_AI] = make_screen(S_AI, "Claude");
    scr[S_COIN] = make_screen(S_COIN, "Coin");
    scr[S_SPLASH] = make_screen(S_SPLASH, "");
    scr[S_SETTINGS] = make_screen(S_SETTINGS, "Settings");
    build_ai();
    build_coin();
    build_splash();
    build_settings();
    lv_screen_load(scr[S_AI]);
}

void ui_toggle_settings() { on_gear(nullptr); }

void ui_next_screen() {
    if (current == S_SETTINGS) show(previous);
    else show((Screen)((current + 1) % CONTENT_SCREENS));
}

void ui_draw_portal() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_set_style_bg_color(s, COL(K_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_t *l = label(s, &inter_22, K_FG, "Setup");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);
    l = label(s, &inter_16, K_ACCENT, "1. Join this Wi-Fi network");
    lv_obj_set_pos(l, 16, 56);
    l = label(s, &inter_32, K_FG, "ClawTicker-Setup");
    lv_obj_set_pos(l, 16, 80);
    l = label(s, &inter_14, K_DIM, "2. A setup page opens. If not, browse to\n    192.168.4.1");
    lv_obj_set_pos(l, 16, 134);
    l = label(s, &inter_14, K_DIM, "3. Enter your Wi-Fi and your computer's IP");
    lv_obj_set_pos(l, 16, 188);
    lv_screen_load(s);
    for (int i = 0; i < 5; i++) { lv_timer_handler(); delay(20); }
}

void ui_tick() {
    static uint32_t lastRefresh = 0;
    uint32_t now = millis();
    uint32_t interval = current == S_SPLASH ? 50 : 500;
    if (now - lastRefresh >= interval) {
        lastRefresh = now;
        Data d;
        bool have = net_get(d);
        refresh_chrome(current, d, have);
        switch (current) {
            case S_AI: update_ai(d, have); break;
            case S_COIN: update_coin(d, have); break;
            case S_SPLASH: update_splash(d, have); break;
            case S_SETTINGS: update_settings(d, have); break;
            default: break;
        }
    }
    lv_timer_handler();
}

void ui_snapshot_serial() {
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!snap) { Serial.println("shot failed"); return; }
    int w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    Serial.printf("SHOT %d %d\n", w, h);
    for (int y = 0; y < h; y++) Serial.write(snap->data + y * stride, w * 2);
    Serial.println();
    lv_draw_buf_destroy(snap);
}
