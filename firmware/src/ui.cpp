#include "ui.h"
#include <lvgl.h>
#include <math.h>
#include "data.h"
#include "hw.h"
#include "icons.h"
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

static const uint32_t K_CLAUDE = 0xF2691E, K_CODEX = 0x6C8CFF, K_AMBER = 0xF5A623;

enum Screen { S_AI, S_COIN, S_SPLASH, S_SETTINGS, S_ALERT, S_COUNT };
static const int CONTENT_SCREENS = 3;
static const int SESSION_MIN = 300, WEEK_MIN = 10080;   // window lengths, for the pace marker

static lv_obj_t *scr[S_COUNT];
static Screen current = S_AI, previous = S_AI, alertReturn = S_AI;
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

// Blend a toward b, t = 0..255.
static uint32_t mix(uint32_t a, uint32_t b, int t) {
    auto ch = [&](int sh) { return (uint32_t)((((a >> sh) & 255) * (255 - t) + ((b >> sh) & 255) * t) / 255); };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

static bool is_codex(const char *name) { return strcmp(name, "Codex") == 0; }
static uint32_t provider_color(const char *name) { return is_codex(name) ? K_CODEX : K_CLAUDE; }
static const lv_image_dsc_t *provider_icon(const char *name) { return is_codex(name) ? &icon_term16 : &icon_burst16; }

static lv_obj_t *icon(lv_obj_t *parent, const lv_image_dsc_t *src, uint32_t color) {
    lv_obj_t *i = lv_image_create(parent);
    lv_image_set_src(i, src);
    lv_obj_set_style_image_recolor(i, COL(color), 0);
    lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
    return i;
}

// Horizontal gradient fill: base color to a lighter tint of it.
static void gradient(lv_obj_t *o, uint32_t c) {
    lv_obj_set_style_bg_color(o, COL(c), 0);
    lv_obj_set_style_bg_grad_color(o, COL(mix(c, 0xFFFFFF, 80)), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
}

// Rotating status words shown in the footer while data is flowing.
static const char *VERBS[] = {"Pondering", "Tinkering", "Brewing", "Conjuring", "Percolating", "Noodling", "Simmering",
                              "Untangling", "Whittling", "Spelunking", "Marinating", "Cogitating", "Daydreaming",
                              "Doodling", "Fermenting", "Hatching", "Juggling", "Musing", "Polishing", "Weaving"};
static const int VERB_COUNT = sizeof(VERBS) / sizeof(VERBS[0]);

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
    lv_obj_t *title, *clock, *date, *status, *statusIcon, *gear, *dots[CONTENT_SCREENS];
};
static Chrome chrome[S_COUNT];

static void dismiss_alert();

static void on_screen_tap(lv_event_t *) {
    if (current == S_SETTINGS) return;
    if (current == S_ALERT) { dismiss_alert(); return; }
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
    c.statusIcon = icon(s, &icon_burst12, K_ACCENT);
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
    char verb[32];
    snprintf(verb, sizeof(verb), "%s...", VERBS[((millis() / 4500) * 7) % VERB_COUNT]);
    set_text(c.status, fresh ? verb : net_wifi_connected() ? "No data" : "Connecting...");
    set_color(c.status, fresh ? K_ACCENT : K_DIM);
    lv_obj_update_layout(c.status);
    int tw = lv_obj_get_width(c.status), start = fresh ? (SCREEN_W - (tw + 18)) / 2 : (SCREEN_W - tw) / 2;
    lv_obj_align(c.status, LV_ALIGN_TOP_LEFT, start + (fresh ? 18 : 0), SCREEN_H - 24);
    if (fresh) { lv_obj_remove_flag(c.statusIcon, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(c.statusIcon, start, SCREEN_H - 22); }
    else lv_obj_add_flag(c.statusIcon, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < CONTENT_SCREENS; i++)
        lv_obj_set_style_bg_color(c.dots[i], COL(id != S_SETTINGS && i == (int)id ? K_ACCENT : K_BORDER), 0);
}

// ---- AI usage screen ------------------------------------------------------

struct Meter {
    lv_obj_t *pct, *reset, *track, *fill, *mark;
};
struct CardUI {
    lv_obj_t *root, *name, *off, *icon;
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
    lv_obj_set_style_bg_opa(c.root, LV_OPA_COVER, 0);
    c.icon = icon(c.root, &icon_burst16, K_CLAUDE);
    lv_obj_set_pos(c.icon, 12, 9);
    c.name = label(c.root, &inter_14, K_FG);
    lv_obj_set_pos(c.name, 33, 8);
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

static void update_meter(Meter &m, int pct, int resetMin, int windowMin, bool off, bool weekly, int cardW, uint32_t tint) {
    char buf[16];
    int trackW = cardW - 26;
    snprintf(buf, sizeof(buf), off ? "--" : "%d%%", pct);
    set_text(m.pct, buf);
    set_color(m.pct, weekly && !off ? K_DIM : K_FG);
    fmt_short(buf, sizeof(buf), off ? -1 : resetMin);
    set_text(m.reset, off ? "" : buf);
    lv_obj_set_x(m.reset, cardW - 14 - lv_obj_get_width(m.reset));
    uint32_t base = pct >= 85 ? K_BAD : pct >= 60 ? K_AMBER : tint;
    gradient(m.fill, weekly ? mix(K_BG, base, 150) : base);
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
        uint32_t tint = provider_color(c.name);
        lv_obj_set_style_bg_color(cards[i].root, COL(mix(K_BG, tint, 20)), 0);
        lv_obj_set_style_border_color(cards[i].root, COL(mix(0x303030, tint, 110)), 0);
        lv_image_set_src(cards[i].icon, provider_icon(c.name));
        lv_obj_set_style_image_recolor(cards[i].icon, COL(tint), 0);
        set_text(cards[i].name, multi ? c.name : "Plan usage");
        if (c.off) lv_obj_remove_flag(cards[i].off, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cards[i].off, LV_OBJ_FLAG_HIDDEN);
        update_meter(cards[i].session, c.session, c.sessionReset, SESSION_MIN, c.off, false, w, tint);
        update_meter(cards[i].weekly, c.weekly, c.weeklyReset, WEEK_MIN, c.off, true, w, tint);
    }
}

// ---- Coin screen ----------------------------------------------------------

struct CoinRow { lv_obj_t *badge, *letter, *pair, *price, *change; };
static CoinRow coinRows[4];
static lv_obj_t *coin_empty;

static uint32_t coin_color(const char *sym) {
    if (!strcmp(sym, "BTC")) return 0xF7931A;
    if (!strcmp(sym, "ETH")) return 0x627EEA;
    if (!strcmp(sym, "SOL")) return 0x9945FF;
    if (!strcmp(sym, "SUI")) return 0x4DA2FF;
    if (!strcmp(sym, "XRP")) return 0x3A7BC8;
    if (!strcmp(sym, "DOGE")) return 0xC2A633;
    static const uint32_t PALETTE[] = {0x2BB673, 0xE0527A, 0x13A8B8, 0xB06AD8, 0xD98B2B};
    uint32_t h = 0;
    for (const char *p = sym; *p; p++) h = h * 31 + *p;
    return PALETTE[h % 5];
}

static void build_coin() {
    lv_obj_t *s = scr[S_COIN];
    lv_obj_t *h1 = label(s, &inter_14, K_DIM, "Coin");
    lv_obj_set_pos(h1, 12, 52);
    lv_obj_t *h2 = label(s, &inter_14, K_DIM, "Price");
    lv_obj_align(h2, LV_ALIGN_TOP_LEFT, 150, 52);
    lv_obj_t *h3 = label(s, &inter_14, K_DIM, "24h");
    lv_obj_align(h3, LV_ALIGN_TOP_RIGHT, -12, 52);
    for (int i = 0; i < 4; i++) {
        coinRows[i].badge = box(s, 12, 0, 26, 26, K_TRACK, LV_RADIUS_CIRCLE);
        coinRows[i].letter = label(coinRows[i].badge, &inter_14, 0xFFFFFF, "?");
        lv_obj_center(coinRows[i].letter);
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
            lv_obj_add_flag(r.badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.pair, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.price, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r.change, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(r.badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r.pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r.price, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r.change, LV_OBJ_FLAG_HIDDEN);
        const Coin &c = d.coins[i];
        char buf[32], num[24];
        int y = 84 + i * rowH;
        snprintf(buf, sizeof(buf), "%s/USD", c.sym);
        set_text(r.pair, buf);
        lv_obj_set_pos(r.pair, 46, y + 2);
        lv_obj_set_y(r.badge, y - 2);
        lv_obj_set_style_bg_color(r.badge, COL(coin_color(c.sym)), 0);
        char one[2] = {c.sym[0], 0};
        set_text(r.letter, one);
        lv_obj_center(r.letter);
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

// ---- Mascot (splash + alert) ----------------------------------------------

struct Mascot { lv_obj_t *shadow, *body, *armL, *armR, *legL, *legR, *eyeL, *eyeR, *drop; };

static Mascot build_mascot(lv_obj_t *p) {
    Mascot m;
    m.shadow = box(p, 0, 0, 80, 8, 0x1A1A1A, LV_RADIUS_CIRCLE);
    m.legL = box(p, 0, 0, 14, 12, K_DIM);
    m.legR = box(p, 0, 0, 14, 12, K_DIM);
    m.armL = box(p, 0, 0, 16, 22, K_DIM, 6);
    m.armR = box(p, 0, 0, 16, 22, K_DIM, 6);
    m.body = box(p, 0, 0, 96, 80, K_DIM, 14);
    m.eyeL = box(p, 0, 0, 12, 18, K_BG, 2);
    m.eyeR = box(p, 0, 0, 12, 18, K_BG, 2);
    m.drop = box(p, 0, 0, 6, 9, 0x6EAAFF, LV_RADIUS_CIRCLE);
    return m;
}

// One hop every 1/rate seconds: stretch in the air, squash on landing, arms up.
static void update_mascot(Mascot &m, int cx, int ground, float t, uint32_t color, float rate, bool sweat) {
    float p = fmodf(t * rate, 1.0f);
    float air = 0, squash = 0;
    if (p < 0.7f) { float q = p / 0.7f; air = 4 * q * (1 - q); }
    else squash = sinf((p - 0.7f) / 0.3f * (float)M_PI);
    float sy = 1.0f + 0.06f * air - 0.16f * squash;
    int bodyH = lroundf(80 * sy), bodyW = lroundf(96 * (2.0f - sy));
    int lift = lroundf(air * 34);
    int feet = ground - lift, bodyBottom = feet - 12, bodyY = bodyBottom - bodyH, bodyX = cx - bodyW / 2;
    lv_obj_set_size(m.body, bodyW, bodyH);
    lv_obj_set_pos(m.body, bodyX, bodyY);
    lv_obj_set_pos(m.legL, cx - 30, bodyBottom - 1);
    lv_obj_set_pos(m.legR, cx + 16, bodyBottom - 1);
    int armY = bodyY + bodyH * 32 / 100 - lroundf(air * 14);
    lv_obj_set_pos(m.armL, bodyX - 14, armY);
    lv_obj_set_pos(m.armR, bodyX + bodyW - 2, armY);
    bool blink = fmodf(t, 3.4f) > 3.25f;
    int eyeH = blink ? 3 : 18;
    int eyeY = bodyY + bodyH * 35 / 100 + (blink ? 8 : 0);
    lv_obj_set_size(m.eyeL, 12, eyeH);
    lv_obj_set_size(m.eyeR, 12, eyeH);
    lv_obj_set_pos(m.eyeL, bodyX + bodyW * 25 / 100, eyeY);
    lv_obj_set_pos(m.eyeR, bodyX + bodyW * 62 / 100, eyeY);
    int sw = lroundf(80 * (1.0f - 0.4f * air));
    lv_obj_set_size(m.shadow, sw, 8);
    lv_obj_set_pos(m.shadow, cx - sw / 2, ground - 3);
    for (lv_obj_t *o : {m.body, m.armL, m.armR, m.legL, m.legR}) lv_obj_set_style_bg_color(o, COL(color), 0);
    if (sweat) { lv_obj_remove_flag(m.drop, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(m.drop, bodyX + bodyW - 10, bodyY + 4 + (int)fmodf(t * 22, 16)); }
    else lv_obj_add_flag(m.drop, LV_OBJ_FLAG_HIDDEN);
}

// ---- Splash ---------------------------------------------------------------

static Mascot splashMascot;
static lv_obj_t *splash_sub;

static void build_splash() {
    lv_obj_t *s = scr[S_SPLASH];
    splashMascot = build_mascot(s);
    lv_obj_t *name = label(s, &inter_22, K_FG, "ClawTicker");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 152);
    splash_sub = label(s, &inter_14, K_DIM, "Waiting for data");
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 184);
}

static void update_splash(const Data &d, bool have) {
    int pct = have && d.cardCount && !d.cards[0].off ? d.cards[0].session : 0;
    uint32_t body = !have ? K_DIM : pct >= 85 ? K_BAD : pct >= 60 ? K_AMBER : K_GOOD;
    float rate = pct >= 85 ? 1.3f : pct >= 60 ? 0.9f : 0.55f;
    update_mascot(splashMascot, 160, 142, millis() / 1000.0f, body, rate, pct >= 85);
    char buf[40];
    if (have && d.cardCount && !d.cards[0].off) snprintf(buf, sizeof(buf), "%s %d%% of session", d.cards[0].name, pct);
    else snprintf(buf, sizeof(buf), "Waiting for data");
    set_text(splash_sub, buf);
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 184);
}

// ---- Alert: an agent just finished a turn ----------------------------------

static Mascot alertMascot;
static lv_obj_t *alert_icon, *alert_title, *alert_sub;
static uint32_t alertUntil = 0, alertTint = K_CLAUDE;
static const uint32_t ALERT_MS = 30000;

static void build_alert() {
    lv_obj_t *s = scr[S_ALERT];
    alertMascot = build_mascot(s);
    alert_icon = icon(s, &icon_burst40, K_CLAUDE);
    lv_obj_set_pos(alert_icon, 14, 12);
    alert_title = label(s, &inter_22, K_FG, "");
    alert_sub = label(s, &inter_14, K_DIM, "Your turn. Tap to dismiss");
    lv_obj_align(alert_sub, LV_ALIGN_TOP_MID, 0, 192);
}

static void show_alert(const char *kind) {
    bool codex = strcmp(kind, "codex") == 0;
    alertTint = codex ? K_CODEX : K_CLAUDE;
    lv_obj_set_style_bg_color(scr[S_ALERT], COL(mix(K_BG, alertTint, 38)), 0);
    lv_image_set_src(alert_icon, codex ? &icon_term40 : &icon_burst40);
    lv_obj_set_style_image_recolor(alert_icon, COL(alertTint), 0);
    set_text(alert_title, codex ? "Codex finished" : "Claude finished");
    lv_obj_align(alert_title, LV_ALIGN_TOP_MID, 0, 158);
    if (current != S_ALERT) alertReturn = current;
    alertUntil = millis() + ALERT_MS;
    hw_set_brightness(255);
    show(S_ALERT);
}

static void dismiss_alert() {
    hw_set_brightness(BRIGHTNESS_LEVELS[cfg.brightness]);
    show(alertReturn == S_ALERT ? S_AI : alertReturn);
}

void ui_test_alert(const char *kind) { show_alert(kind); }

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
    scr[S_ALERT] = lv_obj_create(nullptr);
    lv_obj_remove_style_all(scr[S_ALERT]);
    lv_obj_set_size(scr[S_ALERT], SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(scr[S_ALERT], LV_OPA_COVER, 0);
    lv_obj_add_flag(scr[S_ALERT], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr[S_ALERT], on_screen_tap, LV_EVENT_CLICKED, nullptr);
    build_ai();
    build_coin();
    build_splash();
    build_settings();
    build_alert();
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
    static bool alertInit = false;
    static uint16_t lastAlert = 0;
    uint32_t now = millis();
    bool animated = current == S_SPLASH || current == S_ALERT;
    if (now - lastRefresh >= (animated ? 40u : 500u)) {
        lastRefresh = now;
        Data d;
        bool have = net_get(d);
        if (have) {
            if (!alertInit) { lastAlert = d.alertId; alertInit = true; }   // no alert for a finish from before boot
            else if (d.alertId != lastAlert && d.alertId > 0) { lastAlert = d.alertId; show_alert(d.alertKind); }
        }
        if (current == S_ALERT) {
            update_mascot(alertMascot, 160, 142, now / 1000.0f, alertTint, 1.1f, false);
            if (now > alertUntil) dismiss_alert();
        } else refresh_chrome(current, d, have);
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
