#include "ui.h"
#include <lvgl.h>
#include <math.h>
#include "data.h"
#include "hw.h"
#include "icons.h"
#include "net.h"
#include "pins.h"
#include "settings.h"
#include "sprites.h"

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

static uint8_t logoPixels[LOGO_BYTES];
static lv_image_dsc_t logoDsc;
static bool hasLogo = false;

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
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the screen (next screen)
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

// ---- chrome shared by every screen (header + footer) ----------------------

struct Chrome {
    lv_obj_t *title, *clock, *status, *statusIcon, *gear, *dots[CONTENT_SCREENS];
    int verbIdx;
};
static Chrome chrome[S_COUNT];

// Looping animations for the footer: the star turns continuously and each new word fades in.
static void anim_rotate(void *obj, int32_t v) { lv_image_set_rotation((lv_obj_t *)obj, v); }
static void anim_fade(void *obj, int32_t v) { lv_obj_set_style_text_opa((lv_obj_t *)obj, v, 0); }

static void spin(lv_obj_t *img) {
    lv_image_set_pivot(img, 6, 6);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, anim_rotate);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 3200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void fade_in(lv_obj_t *label) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, anim_fade);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, 350);
    lv_anim_start(&a);
}

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

// Horizontal flex container, children centered on one common line.
static lv_obj_t *hrow(lv_obj_t *parent, int x, int y, int w, int h, int gap, lv_flex_align_t main) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// Layout grid: 8 px side margins (same as the cards), header 44 px and footer 26 px
// tall, everything in a row centered on the row's middle line.
static lv_obj_t *make_screen(Screen id, const char *title) {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s, COL(K_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, on_screen_tap, LV_EVENT_CLICKED, nullptr);
    Chrome &c = chrome[id];

    lv_obj_t *head = hrow(s, 8, 0, SCREEN_W - 16, 44, 10, LV_FLEX_ALIGN_START);
    if (hasLogo) {   // custom logo uploaded on the setup page
        lv_obj_t *li = lv_image_create(head);
        lv_image_set_src(li, &logoDsc);
    } else {         // default logo: ring with two eyes and a mouth
        lv_obj_t *ring = box(head, 0, 0, 32, 32, K_BG, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(ring, COL(K_FG), 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        box(ring, 8, 8, 4, 8, K_FG, 1);
        box(ring, 16, 8, 4, 8, K_FG, 1);
        box(ring, 8, 19, 12, 2, K_FG, 1);
    }
    c.title = label(head, &inter_18, K_FG, title);
    lv_obj_set_flex_grow(c.title, 1);
    lv_label_set_long_mode(c.title, LV_LABEL_LONG_CLIP);
    c.clock = label(head, &inter_22, K_FG);
    lv_obj_set_width(c.clock, 80);   // fixed width so the digits do not nudge the layout
    lv_label_set_long_mode(c.clock, LV_LABEL_LONG_CLIP);   // never wrap onto a second line
    lv_obj_set_style_text_align(c.clock, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *foot = hrow(s, 8, SCREEN_H - 28, SCREEN_W - 16, 26, 0, LV_FLEX_ALIGN_START);
    lv_obj_t *left = hrow(foot, 0, 0, 72, 26, 6, LV_FLEX_ALIGN_START);
    for (int i = 0; i < CONTENT_SCREENS; i++) c.dots[i] = box(left, 0, 0, 6, 6, K_BORDER, LV_RADIUS_CIRCLE);
    lv_obj_t *mid = hrow(foot, 0, 0, 0, 26, 6, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(mid, 1);
    c.statusIcon = icon(mid, &icon_burst12, K_ACCENT);
    spin(c.statusIcon);
    c.verbIdx = -1;
    c.status = label(mid, &inter_14, K_DIM);
    lv_obj_t *right = hrow(foot, 0, 0, 72, 26, 0, LV_FLEX_ALIGN_END);
    c.gear = label(right, &lv_font_montserrat_14, K_DIM, id == S_SETTINGS ? LV_SYMBOL_CLOSE : LV_SYMBOL_SETTINGS);
    lv_obj_add_flag(c.gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c.gear, 22);
    lv_obj_add_event_cb(c.gear, on_gear, LV_EVENT_CLICKED, nullptr);
    return s;
}

static void refresh_chrome(Screen id, const Data &d, bool have) {
    Chrome &c = chrome[id];
    if (have && d.localTime) {
        uint32_t t = d.localTime + (millis() - d.receivedAt) / 1000;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
        set_text(c.clock, buf);
    }
    bool fresh = have && net_age_ms() < 20000;
    int verbIdx = ((millis() / 4500) * 7) % VERB_COUNT;
    char verb[32];
    snprintf(verb, sizeof(verb), "%s...", VERBS[verbIdx]);
    set_text(c.status, fresh ? verb : net_wifi_connected() ? "No data" : "Connecting...");
    if (fresh && verbIdx != c.verbIdx) { c.verbIdx = verbIdx; fade_in(c.status); }
    set_color(c.status, fresh ? K_ACCENT : K_DIM);
    if (fresh) lv_obj_remove_flag(c.statusIcon, LV_OBJ_FLAG_HIDDEN);
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
static const int CARD_Y = 52, CARD_H = 154, BAR_H = 12;


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
    lv_obj_remove_flag(c.root, LV_OBJ_FLAG_CLICKABLE);
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
    waiting_box = box(s, 0, 48, SCREEN_W, 158, K_BG);
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
    for (int i = 0; i < 2; i++) {
        bool shown = live && (i == 0 || multi);
        if (!shown) { lv_obj_add_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN);
        int w = multi ? 148 : 304, x = multi ? (i == 0 ? 8 : 164) : 8;
        layout_card(cards[i], x, w);
        const Card &c = d.cards[i];
        uint32_t tint = provider_color(c.name);
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

struct CoinRow { lv_obj_t *sym, *price, *change, *divider; };
static CoinRow coinRows[4];
static lv_obj_t *coin_card, *coin_head[3], *coin_empty;
static const int COIN_PRICE_X = 196, COIN_CHANGE_X = 292;   // right edges inside the card

static void build_coin() {
    lv_obj_t *s = scr[S_COIN];
    coin_card = lv_obj_create(s);
    lv_obj_remove_style_all(coin_card);
    lv_obj_set_pos(coin_card, 8, CARD_Y);
    lv_obj_set_size(coin_card, 304, CARD_H);
    lv_obj_set_style_border_color(coin_card, COL(K_BORDER), 0);
    lv_obj_set_style_border_width(coin_card, 1, 0);
    lv_obj_set_style_radius(coin_card, 10, 0);
    lv_obj_remove_flag(coin_card, LV_OBJ_FLAG_CLICKABLE);
    const char *heads[3] = {"Coin", "Price", "24h"};
    for (int i = 0; i < 3; i++) coin_head[i] = label(coin_card, &inter_14, K_DIM, heads[i]);
    lv_obj_set_pos(coin_head[0], 12, 8);
    for (int i = 0; i < 4; i++) {
        coinRows[i].divider = box(coin_card, 12, 0, 280, 1, K_TRACK);
        coinRows[i].sym = label(coin_card, &inter_16, K_FG);
        coinRows[i].price = label(coin_card, &inter_18, K_FG);
        coinRows[i].change = label(coin_card, &inter_14, K_FG);
    }
    coin_empty = label(s, &inter_18, K_DIM, "Waiting for data");
    lv_obj_align(coin_empty, LV_ALIGN_CENTER, 0, -6);
}

static void right_at(lv_obj_t *l, int rightX, int y) { lv_obj_set_pos(l, rightX - lv_obj_get_width(l), y); }

static void update_coin(const Data &d, bool have) {
    bool live = have && net_age_ms() < 180000 && d.coinCount > 0;
    if (live) { lv_obj_add_flag(coin_empty, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(coin_card, LV_OBJ_FLAG_HIDDEN); }
    else {
        lv_obj_remove_flag(coin_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(coin_card, LV_OBJ_FLAG_HIDDEN);
        set_text(coin_empty, have ? "No coin data yet" : "Waiting for data");
        lv_obj_align(coin_empty, LV_ALIGN_CENTER, 0, -6);
        return;
    }
    // column headers share the baseline of the rows below
    lv_obj_update_layout(coin_head[1]);
    right_at(coin_head[1], COIN_PRICE_X, 8);
    right_at(coin_head[2], COIN_CHANGE_X, 8);
    int rowH = d.coinCount == 1 ? 100 : d.coinCount == 2 ? 56 : 120 / d.coinCount, y0 = 30;   // rows share the 120 px under the header
    for (int i = 0; i < 4; i++) {
        CoinRow &r = coinRows[i];
        bool shown = i < d.coinCount;
        for (lv_obj_t *o : {r.sym, r.price, r.change}) {
            if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        if (shown && i > 0) { lv_obj_remove_flag(r.divider, LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(r.divider, y0 + i * rowH - 4); }
        else lv_obj_add_flag(r.divider, LV_OBJ_FLAG_HIDDEN);
        if (!shown) continue;
        const Coin &c = d.coins[i];
        char buf[32], num[24];
        int y = y0 + i * rowH + (rowH - 22) / 2 - 2;   // rows are vertically centered in their band
        set_text(r.sym, c.sym);
        lv_obj_set_pos(r.sym, 12, y + 2);
        fmt_price(num, sizeof(num), c.price);
        snprintf(buf, sizeof(buf), "$%s", num);
        set_text(r.price, buf);
        lv_obj_update_layout(r.price);
        right_at(r.price, COIN_PRICE_X, y);
        snprintf(buf, sizeof(buf), "%s%.2f%%", c.change24h >= 0 ? "+" : "", c.change24h);
        set_text(r.change, buf);
        set_color(r.change, c.change24h >= 0 ? K_GOOD : K_BAD);
        lv_obj_update_layout(r.change);
        right_at(r.change, COIN_CHANGE_X, y + 3);
    }
}

// ---- Sprite player (ClaudePix 20x20 animations) -------------------------------

struct Player {
    lv_obj_t *img;
    lv_image_dsc_t dsc;
    uint8_t buf[SPRITE_SIZE * SPRITE_SIZE * 4];   // ARGB8888, rewritten for every frame
    int preset = 0, frame = 0, loops = 0;
    uint32_t nextAt = 0;
    int baseX = 0, baseY = 0;
};

static int sprite_index(const char *slug) {
    for (int i = 0; i < SPRITE_COUNT; i++) if (!strcmp(SPRITES[i].slug, slug)) return i;
    return 0;
}

static void player_draw(Player &p) {
    const sprite_t &sp = SPRITES[p.preset];
    const uint8_t *px = sp.pixels + p.frame * SPRITE_SIZE * SPRITE_SIZE;
    for (int i = 0; i < SPRITE_SIZE * SPRITE_SIZE; i++) {
        const uint8_t *c = sp.palette + px[i] * 4;
        p.buf[i * 4 + 0] = c[2];
        p.buf[i * 4 + 1] = c[1];
        p.buf[i * 4 + 2] = c[0];
        p.buf[i * 4 + 3] = c[3];
    }
    lv_obj_invalidate(p.img);
}

// scale 6 draws each sprite pixel as 6x6 screen pixels. (cx, cy) is the sprite center.
static void player_init(Player &p, lv_obj_t *parent, int cx, int cy, int scale) {
    memset(&p.dsc, 0, sizeof(p.dsc));
    p.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    p.dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    p.dsc.header.w = p.dsc.header.h = SPRITE_SIZE;
    p.dsc.header.stride = SPRITE_SIZE * 4;
    p.dsc.data_size = sizeof(p.buf);
    p.dsc.data = p.buf;
    p.img = lv_image_create(parent);
    lv_image_set_src(p.img, &p.dsc);
    lv_image_set_pivot(p.img, SPRITE_SIZE / 2, SPRITE_SIZE / 2);
    lv_image_set_scale(p.img, 256 * scale);
    lv_image_set_antialias(p.img, false);   // keep the pixels square
    p.baseX = cx - SPRITE_SIZE / 2;
    p.baseY = cy - SPRITE_SIZE / 2;
    lv_obj_set_pos(p.img, p.baseX, p.baseY);
    player_draw(p);
}

static void player_set(Player &p, int preset) {
    p.preset = preset;
    p.frame = p.loops = 0;
    p.nextAt = 0;
}

// ClaudePix idles only run at 3 or 4 frames per second, which looks steppy on its own.
// A slow vertical float on top keeps the creature visibly moving between frames.
static void player_float(Player &p, uint32_t now, float amplitude, float period_s) {
    int off = lroundf(amplitude * sinf(now / 1000.0f * 2.0f * (float)M_PI / period_s));
    lv_obj_set_y(p.img, p.baseY + off);
}

// Advance when the frame's hold time has passed. Returns true when a loop just finished.
static bool player_tick(Player &p, uint32_t now) {
    if (p.nextAt && now < p.nextAt) return false;
    const sprite_t &sp = SPRITES[p.preset];
    bool wrapped = false;
    if (p.nextAt) {
        p.frame = (p.frame + 1) % sp.frames;
        if (p.frame == 0) { p.loops++; wrapped = true; }
    }
    p.nextAt = now + sp.hold_ms[p.frame];
    player_draw(p);
    return wrapped;
}

// ---- Splash ---------------------------------------------------------------

static Player splashPlayer;
static lv_obj_t *splash_sub;
static int splashMood = -1, splashPick = 0;

// Which animations suit each mood; the splash rotates through its list.
static const char *ANIM_CALM[] = {"idle_breathe", "idle_blink", "idle_look_around", "expression_wink", "dance_sway", "idle_breathe"};
static const char *ANIM_BUSY[] = {"work_coding", "work_think"};
static const char *ANIM_HOT[] = {"expression_surprise", "work_think"};
static const char *ANIM_NODATA[] = {"expression_sleep"};

static void build_splash() {
    lv_obj_t *s = scr[S_SPLASH];
    player_init(splashPlayer, s, 160, 92, 6);
    player_set(splashPlayer, sprite_index("idle_breathe"));
    lv_obj_t *name = label(s, &inter_22, K_FG, "ClawTicker");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 156);
    splash_sub = label(s, &inter_14, K_DIM, "Waiting for data");
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 184);
}

static void update_splash(const Data &d, bool have) {
    int pct = have && d.cardCount && !d.cards[0].off ? d.cards[0].session : 0;
    int mood = !have ? 3 : pct >= 85 ? 2 : pct >= 60 ? 1 : 0;
    const char **list = mood == 3 ? ANIM_NODATA : mood == 2 ? ANIM_HOT : mood == 1 ? ANIM_BUSY : ANIM_CALM;
    int n = mood == 3 ? 1 : mood == 2 ? 2 : mood == 1 ? 2 : 6;
    if (mood != splashMood) { splashMood = mood; splashPick = 0; player_set(splashPlayer, sprite_index(list[0])); }
    player_float(splashPlayer, millis(), mood == 2 ? 4.0f : 3.0f, mood == 2 ? 0.9f : 2.6f);
    if (player_tick(splashPlayer, millis()) && splashPlayer.loops >= 2 && n > 1) {   // rotate after two plays
        splashPick = (splashPick + 1) % n;
        player_set(splashPlayer, sprite_index(list[splashPick]));
    }
    char buf[40];
    if (have && d.cardCount && !d.cards[0].off) snprintf(buf, sizeof(buf), "%s %d%% of session", d.cards[0].name, pct);
    else snprintf(buf, sizeof(buf), "Waiting for data");
    set_text(splash_sub, buf);
    lv_obj_align(splash_sub, LV_ALIGN_TOP_MID, 0, 184);
}

// ---- Alert: an agent just finished a turn ----------------------------------

static Player alertPlayer;
static lv_obj_t *alert_title, *alert_sub;
static uint32_t alertUntil = 0;
static const uint32_t ALERT_MS = 6000;   // auto-close if nobody taps

static void build_alert() {
    lv_obj_t *s = scr[S_ALERT];
    player_init(alertPlayer, s, 160, 92, 6);
    player_set(alertPlayer, sprite_index("dance_bounce"));
    alert_title = label(s, &inter_22, K_FG, "");
    alert_sub = label(s, &inter_14, K_DIM, "Your turn. Tap to dismiss");
    lv_obj_align(alert_sub, LV_ALIGN_TOP_MID, 0, 196);
}

static void show_alert(const char *kind) {
    bool codex = strcmp(kind, "codex") == 0;
    set_text(alert_title, codex ? "Codex finished" : "Claude finished");
    lv_obj_align(alert_title, LV_ALIGN_TOP_MID, 0, 164);
    if (current != S_ALERT) alertReturn = current;
    alertUntil = millis() + ALERT_MS;
    player_set(alertPlayer, sprite_index("dance_bounce"));
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

static bool lastMulti = false;   // what the board is showing right now

static void on_display(lv_event_t *) {
    cfg.display = lastMulti ? 0 : 1;   // flip, and remember it (the computer is told on the next poll)
    settings_save();
    lastMulti = cfg.display == 1;
    set_text(display_val, lastMulti ? "Claude + Codex" : "Claude only");
    lv_obj_center(display_val);
    net_poll_now();
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
    lv_obj_set_pos(l, 9, y + 5);
}

static void build_settings() {
    lv_obj_t *s = scr[S_SETTINGS];
    setting_row(s, 54, "Brightness");
    lv_obj_t *minus = pill(s, 200, 54, 34, "-", nullptr);
    lv_obj_t *plus = pill(s, 278, 54, 34, "+", nullptr);
    lv_obj_t *mid = label(s, &inter_14, K_FG, "");
    bright_val = mid;
    lv_obj_set_pos(mid, 242, 61);
    for (lv_obj_t *b : {minus, plus}) lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(minus, on_bright, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(plus, on_bright, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    setting_row(s, 94, "Display");
    lv_obj_t *dp = pill(s, 174, 94, 138, "", &display_val);
    lv_obj_add_flag(dp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dp, on_display, LV_EVENT_CLICKED, nullptr);
    setting_row(s, 134, "Computer");
    pill(s, 154, 134, 158, "", &host_val);
    setting_row(s, 174, "Wi-Fi");
    lv_obj_t *rp = pill(s, 182, 174, 130, "Reset setup", &reset_lbl);
    lv_obj_add_flag(rp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rp, on_reset, LV_EVENT_CLICKED, nullptr);
}

static void update_settings(const Data &d, bool have) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%d/4", cfg.brightness + 1);
    set_text(bright_val, buf);
    lv_obj_set_pos(bright_val, 256 - lv_obj_get_width(bright_val) / 2, 61);
    if (have) lastMulti = d.multi;
    set_text(display_val, lastMulti ? "Claude + Codex" : "Claude only");
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

    hasLogo = settings_load_logo(logoPixels);
    if (hasLogo) {
        logoDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        logoDsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        logoDsc.header.w = logoDsc.header.h = LOGO_SIZE;
        logoDsc.header.stride = LOGO_SIZE * 4;
        logoDsc.data_size = LOGO_BYTES;
        logoDsc.data = logoPixels;
    }
    scr[S_AI] = make_screen(S_AI, "Claude");
    scr[S_COIN] = make_screen(S_COIN, "Coin");
    scr[S_SPLASH] = make_screen(S_SPLASH, "");
    scr[S_SETTINGS] = make_screen(S_SETTINGS, "Settings");
    scr[S_ALERT] = lv_obj_create(nullptr);
    lv_obj_remove_style_all(scr[S_ALERT]);
    lv_obj_set_size(scr[S_ALERT], SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(scr[S_ALERT], COL(K_BG), 0);
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
            player_tick(alertPlayer, now);
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
