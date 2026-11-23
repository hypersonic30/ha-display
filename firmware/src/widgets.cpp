#include "widgets.h"
#include "ha_client.h"
#include "fonts.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <ctime>
#include <strings.h>

// The wire format is described in docs/protocol.md.

// ── Types & storage ───────────────────────────────────────────────────────────

enum WidgetType {
    WT_LABEL, WT_BUTTON, WT_GAUGE, WT_SWITCH, WT_BAR,
    WT_SLIDER, WT_ARC, WT_LED, WT_CLOCK, WT_DROPDOWN, WT_ROLLER, WT_CHECKBOX, WT_PANEL, WT_MEDIA, WT_THERMOSTAT
};

enum ClockFormat { CF_TIME, CF_TIME_S, CF_DATE, CF_DATETIME };

#define MAX_WIDGETS 32

struct Widget {
    char       id[16];
    WidgetType type;
    char       entity_id[64];
    char       unit[16];
    char       title[32];
    char       attr[24];
    uint8_t    format;
    int        font;                     // requested size of the main text, 0 = automatic
    int        min_val, max_val;
    int        scale;                    // slider/arc/bar: integer steps per unit (10 for small ranges)
    lv_color_t bg, fg, on;
    bool       has_bg, has_fg, has_on;
    float      warn, crit;
    bool       has_warn, has_crit, invert;
    const char *icon;                    // LVGL symbol (static string) or nullptr
    // Values received from the integration (see "States" in the protocol)
    char       state[48];                // entity
    char       v1[48];                   // entity|attr, |value, |option, |media_title, |current_temperature
    char       v2[48];                   // |media_artist, |temperature
    char       v3[16];                   // media: |value (volume)
    uint32_t   last_sent, lock_until;
    int        last_raw;
    // LVGL handles
    lv_obj_t  *root, *title_lbl, *value_lbl;
    lv_obj_t  *ctl;                      // main control (switch, bar, arc, slider, checkbox, dropdown, roller, led)
    lv_obj_t  *aux1, *aux2, *aux3;       // media: prev / play / next; thermostat: minus / plus
    lv_obj_t  *aux_lbl;                  // media: artist; thermostat: current temperature
    lv_obj_t  *aux_lbl2;                 // media: play button symbol; thermostat: mode text
};

static Widget _w[MAX_WIDGETS];
static int    _count = 0;

// ── Shared styles (created once) ──────────────────────────────────────────────

static lv_style_t s_card, s_title, s_value, s_unit;
static bool       _styles_init = false;

static const lv_color_t C_BG     = LV_COLOR_MAKE(0x0f, 0x0f, 0x1a);
static const lv_color_t C_CARD   = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
static const lv_color_t C_ACCENT = LV_COLOR_MAKE(0x4f, 0xc3, 0xf7);
static const lv_color_t C_MUTED  = LV_COLOR_MAKE(0x77, 0x77, 0x99);
static const lv_color_t C_WHITE  = LV_COLOR_MAKE(0xe0, 0xe0, 0xe0);
static const lv_color_t C_ON     = LV_COLOR_MAKE(0x4f, 0xc3, 0xf7);
static const lv_color_t C_OFF    = LV_COLOR_MAKE(0x2a, 0x2a, 0x44);
static const lv_color_t C_WARN   = LV_COLOR_MAKE(0xff, 0xb3, 0x00);
static const lv_color_t C_CRIT   = LV_COLOR_MAKE(0xf4, 0x43, 0x36);

static void init_styles() {
    if (_styles_init) return;

    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, C_CARD);
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_radius(&s_card, 10);
    lv_style_set_pad_all(&s_card, 8);

    lv_style_init(&s_title);
    lv_style_set_text_color(&s_title, C_MUTED);
    lv_style_set_text_font(&s_title, &ui_font_12);

    lv_style_init(&s_value);
    lv_style_set_text_color(&s_value, C_WHITE);
    lv_style_set_text_font(&s_value, &ui_font_20);

    lv_style_init(&s_unit);
    lv_style_set_text_color(&s_unit, C_MUTED);
    lv_style_set_text_font(&s_unit, &ui_font_14);

    _styles_init = true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// strlcpy that never leaves half of a UTF-8 sequence at the end
static void copy_utf8(char *dst, const char *src, size_t n) {
    strlcpy(dst, src, n);
    if (strlen(src) < n) return;
    size_t len = strlen(dst);
    if (!len) return;
    size_t pos = len - 1;
    while (pos > 0 && ((uint8_t)dst[pos] & 0xC0) == 0x80) pos--;
    uint8_t lead = (uint8_t)dst[pos];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (len - pos < need) dst[pos] = 0;
}

static bool parse_num(const char *s, float &out) {
    char *end;
    out = strtof(s, &end);
    return end != s && *end == 0 && std::isfinite(out);
}

static bool parse_color(const char *s, lv_color_t &out) {
    if (!s || s[0] != '#' || strlen(s) != 7) return false;
    char *end;
    uint32_t v = strtoul(s + 1, &end, 16);
    if (*end) return false;
    out = lv_color_hex(v);
    return true;
}

static lv_color_t col_bg(const Widget &wg, lv_color_t def) { return wg.has_bg ? wg.bg : def; }
static lv_color_t col_fg(const Widget &wg, lv_color_t def) { return wg.has_fg ? wg.fg : def; }
static lv_color_t col_on(const Widget &wg)                 { return wg.has_on ? wg.on : C_ON; }

static bool is_active(const char *state) {
    static const char *const ACTIVE[] = {
        "on", "open", "opening", "closing", "playing", "home", "unlocked", "heat", "cool", "heat_cool",
        "auto", "active", "detected", "armed_home", "armed_away", "cleaning"};
    char low[24];
    size_t i = 0;
    for (; state[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)state[i]);
    low[i] = 0;
    for (const char *a : ACTIVE) if (strcmp(low, a) == 0) return true;
    return false;
}

struct IconName { const char *name; const char *symbol; };
static const IconName ICONS[] = {
    {"power", LV_SYMBOL_POWER}, {"home", LV_SYMBOL_HOME}, {"bell", LV_SYMBOL_BELL}, {"play", LV_SYMBOL_PLAY},
    {"pause", LV_SYMBOL_PAUSE}, {"stop", LV_SYMBOL_STOP}, {"next", LV_SYMBOL_NEXT}, {"prev", LV_SYMBOL_PREV},
    {"plus", LV_SYMBOL_PLUS}, {"minus", LV_SYMBOL_MINUS}, {"up", LV_SYMBOL_UP}, {"down", LV_SYMBOL_DOWN},
    {"left", LV_SYMBOL_LEFT}, {"right", LV_SYMBOL_RIGHT}, {"ok", LV_SYMBOL_OK}, {"close", LV_SYMBOL_CLOSE},
    {"settings", LV_SYMBOL_SETTINGS}, {"refresh", LV_SYMBOL_REFRESH}, {"wifi", LV_SYMBOL_WIFI},
    {"battery", LV_SYMBOL_BATTERY_FULL}, {"charge", LV_SYMBOL_CHARGE}, {"warning", LV_SYMBOL_WARNING},
    {"eye", LV_SYMBOL_EYE_OPEN}, {"volume", LV_SYMBOL_VOLUME_MAX}, {"mute", LV_SYMBOL_MUTE},
    {"audio", LV_SYMBOL_AUDIO}, {"video", LV_SYMBOL_VIDEO}, {"list", LV_SYMBOL_LIST}, {"trash", LV_SYMBOL_TRASH},
    {"edit", LV_SYMBOL_EDIT}, {"gps", LV_SYMBOL_GPS}, {"image", LV_SYMBOL_IMAGE}, {"shuffle", LV_SYMBOL_SHUFFLE},
    {"loop", LV_SYMBOL_LOOP}, {"bluetooth", LV_SYMBOL_BLUETOOTH}, {"usb", LV_SYMBOL_USB},
};

static const char *icon_symbol(const char *name) {
    if (!name || !name[0]) return nullptr;
    for (const IconName &i : ICONS) if (strcmp(i.name, name) == 0) return i.symbol;
    return nullptr;
}

// "<icon> <text>", just the icon, just the text, or the fallback
static void compose_text(const Widget &wg, char *out, size_t n, const char *fallback) {
    const char *text = wg.title[0] ? wg.title : (wg.icon ? "" : fallback);
    if (wg.icon && text[0]) snprintf(out, n, "%s  %s", wg.icon, text);
    else if (wg.icon)       strlcpy(out, wg.icon, n);
    else                    strlcpy(out, text, n);
}

// What a label/bar/gauge shows: the attribute when one is selected, else the state.
static const char *main_value(const Widget &wg) { return wg.attr[0] ? wg.v1 : wg.state; }

static void set_value_text(Widget &wg, const char *value) {
    if (!wg.value_lbl) return;
    char buf[64];
    bool tight = wg.unit[0] == '%' || ((uint8_t)wg.unit[0] == 0xC2 && (uint8_t)wg.unit[1] == 0xB0);  // "%", "°C"
    if (wg.unit[0]) snprintf(buf, sizeof(buf), "%s%s%s", value, tight ? "" : " ", wg.unit);
    else            strlcpy(buf, value, sizeof(buf));
    lv_label_set_text(wg.value_lbl, buf);
}

static lv_obj_t *make_card(int x, int y, int w, int h, lv_color_t fill = C_CARD) {
    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(obj, &s_card, LV_PART_MAIN);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, fill, LV_PART_MAIN);
    return obj;
}

// The font for the main text: the automatic one for font 0, else the largest of 12/14/16/20/24 that is <= the request.
static const lv_font_t *font_for(const Widget &wg, const lv_font_t *automatic) {
    if (wg.font <= 0) return automatic;
    static const struct { int size; const lv_font_t *font; } T[] = {
        {24, &ui_font_24}, {20, &ui_font_20}, {16, &ui_font_16}, {14, &ui_font_14}, {12, &ui_font_12}};
    for (const auto &t : T)
        if (wg.font >= t.size) return t.font;
    return &ui_font_12;
}

// One line of at most `width` pixels: longer text ends in dots instead of wrapping or overflowing.
static void clip_line(lv_obj_t *lbl, int width, const lv_font_t *font) {
    if (!lbl) return;
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, width > 8 ? width : 8);
    lv_obj_set_height(lbl, lv_font_get_line_height(font));
}

static lv_obj_t *make_title(Widget &wg, lv_align_t align, int dx = 0, int dy = 0) {
    lv_obj_t *t = lv_label_create(wg.root);
    lv_obj_add_style(t, &s_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, col_fg(wg, C_MUTED), LV_PART_MAIN);
    char buf[64];
    compose_text(wg, buf, sizeof(buf), wg.entity_id);
    lv_label_set_text(t, buf);
    lv_obj_align(t, align, dx, dy);
    wg.title_lbl = t;
    return t;
}

static lv_obj_t *make_value(Widget &wg, lv_align_t align, int dx, int dy, const lv_font_t *font) {
    lv_obj_t *v = lv_label_create(wg.root);
    lv_obj_add_style(v, &s_value, LV_PART_MAIN);
    lv_obj_set_style_text_font(v, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(v, col_fg(wg, C_WHITE), LV_PART_MAIN);
    lv_label_set_text(v, "--");
    lv_obj_align(v, align, dx, dy);
    wg.value_lbl = v;
    return v;
}

static int scale_of(int lo, int hi) { return (hi - lo) <= 20 ? 10 : 1; }

static void style_track(Widget &wg, lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, col_bg(wg, C_OFF), LV_PART_MAIN);
}

// Indicator colour of bar/gauge: `crit` red, `warn` amber, else the `on` colour.
static lv_color_t indicator_color(const Widget &wg, bool have, float v) {
    if (have) {
        auto hit = [&](float limit) { return wg.invert ? v <= limit : v >= limit; };
        if (wg.has_crit && hit(wg.crit)) return C_CRIT;
        if (wg.has_warn && hit(wg.warn)) return C_WARN;
    }
    return col_on(wg);
}

// True while the user is operating the control: server values must not fight the finger.
static bool value_locked(const Widget &wg, lv_obj_t *ctl) {
    if (!ctl) return true;
    if (millis() < wg.lock_until) return true;
    if (wg.type == WT_SLIDER || wg.type == WT_MEDIA) return lv_slider_is_dragged(ctl);
    return false;
}

// ── Local clock (no NTP: epoch + offset from the integration, advanced by millis()) ────────────

static bool     _t_have   = false;
static int64_t  _t_epoch  = 0;
static int32_t  _t_offset = 0;
static uint32_t _t_millis = 0;
static uint32_t _t_last_tick = 0;

static void format_clock(const Widget &wg, char *out, size_t n) {
    if (!_t_have) {
        const char *none = wg.format == CF_TIME ? "--:--" : wg.format == CF_TIME_S ? "--:--:--"
                         : wg.format == CF_DATE ? "--.--.----" : "--.-- --:--";
        strlcpy(out, none, n);
        return;
    }
    int64_t local = _t_epoch + _t_offset + (int64_t)((uint32_t)(millis() - _t_millis) / 1000);
    time_t t = (time_t)local;
    struct tm tm;
    gmtime_r(&t, &tm);
    switch (wg.format) {
    case CF_TIME_S:   snprintf(out, n, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec); break;
    case CF_DATE:     snprintf(out, n, "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900); break;
    case CF_DATETIME: snprintf(out, n, "%02d.%02d. %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min); break;
    default:          snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min); break;
    }
}

static void refresh_clock(Widget &wg) {
    if (!wg.value_lbl) return;
    char text[24];
    format_clock(wg, text, sizeof(text));
    if (strcmp(text, wg.v1) == 0) return;       // v1 caches what is shown
    strlcpy(wg.v1, text, sizeof(wg.v1));
    lv_label_set_text(wg.value_lbl, text);
}

// ── Touch callbacks ───────────────────────────────────────────────────────────

static void style_button(Widget &wg, bool on) {
    lv_obj_set_style_bg_color(wg.root, on ? col_on(wg) : col_bg(wg, C_OFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.value_lbl, wg.has_fg ? wg.fg : (on ? C_BG : C_WHITE), LV_PART_MAIN);
}

static bool optimistic_domain(const char *entity) {
    static const char *const D[] = {"light.", "switch.", "input_boolean.", "fan.", "automation."};
    for (const char *d : D) if (strncmp(entity, d, strlen(d)) == 0) return true;
    return false;
}

static void cb_button_tap(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0]) return;
    ha_client_tap(wg->id);
    // Optimistic flip for plain toggling entities; the states frame corrects it.
    if (optimistic_domain(wg->entity_id)) style_button(*wg, strcmp(wg->state, "on") != 0);
}

static void cb_switch_changed(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0] || !wg->ctl) return;
    ha_client_set(wg->id, lv_obj_has_state(wg->ctl, LV_STATE_CHECKED));
}

// Slider/arc/volume: update the label right away, send while dragging at most every 250 ms and on release.
static void cb_value(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0]) return;
    lv_obj_t *ctl = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int raw = wg->type == WT_ARC ? lv_arc_get_value(ctl) : lv_slider_get_value(ctl);
    uint32_t now = millis();
    wg->lock_until = now + 1500;
    if (wg->type != WT_MEDIA) {
        char buf[24];
        if (wg->scale == 10) snprintf(buf, sizeof(buf), "%.1f", raw / 10.0f);
        else                 snprintf(buf, sizeof(buf), "%d", raw);
        set_value_text(*wg, buf);
    }
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (now - wg->last_sent < 250) return;
    } else if (raw == wg->last_raw && now - wg->last_sent < 1000) {
        return;                                   // release right after the same value went out
    }
    wg->last_sent = now;
    wg->last_raw  = raw;
    float scale = wg->type == WT_MEDIA ? 1.0f : (float)wg->scale;
    ha_client_set_value(wg->id, raw / scale);
}

static void cb_option(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0] || !wg->ctl) return;
    char buf[40];
    if (wg->type == WT_ROLLER) lv_roller_get_selected_str(wg->ctl, buf, sizeof(buf));
    else                       lv_dropdown_get_selected_str(wg->ctl, buf, sizeof(buf));
    wg->lock_until = millis() + 1500;
    ha_client_set_option(wg->id, buf);
}

// The list of a dropdown is a child of the screen: give it the UI font and colours when it opens.
static void cb_dropdown_open(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    lv_obj_t *list = wg && wg->ctl ? lv_dropdown_get_list(wg->ctl) : nullptr;
    if (!list) return;
    lv_obj_set_style_text_font(list, font_for(*wg, &ui_font_16), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, col_fg(*wg, C_WHITE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, col_bg(*wg, C_OFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, col_on(*wg), (lv_style_selector_t)(LV_PART_SELECTED | LV_STATE_CHECKED));
    lv_obj_set_style_text_color(list, C_BG, (lv_style_selector_t)(LV_PART_SELECTED | LV_STATE_CHECKED));
}

static void cb_act(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0]) return;
    lv_obj_t *t = lv_event_get_target(e);
    const char *action = nullptr;
    if (wg->type == WT_MEDIA) {
        if (t == wg->aux1)      action = "prev";
        else if (t == wg->aux2) action = "play_pause";
        else if (t == wg->aux3) action = "next";
    } else if (wg->type == WT_THERMOSTAT) {
        if (t == wg->aux1)      action = "temp_down";
        else if (t == wg->aux2) action = "temp_up";
    }
    if (action) ha_client_act(wg->id, action);
}

// ── Widget builders ───────────────────────────────────────────────────────────

static void build_label(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | 70;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_CARD));
    if (!wg.entity_id[0]) {
        // Static text: no value row, the title is the content.
        lv_obj_t *t = make_title(wg, LV_ALIGN_LEFT_MID);
        lv_obj_set_style_text_font(t, font_for(wg, &ui_font_20), LV_PART_MAIN);
        lv_obj_set_style_text_color(t, col_fg(wg, C_WHITE), LV_PART_MAIN);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, w - 16 > 8 ? w - 16 : 8);
        if (wg.font > 0) clip_line(t, w - 16, font_for(wg, &ui_font_20));
        return;
    }
    make_title(wg, LV_ALIGN_TOP_LEFT);
    const lv_font_t *vf = font_for(wg, &ui_font_20);
    make_value(wg, LV_ALIGN_BOTTOM_LEFT, 0, 0, vf);
    if (wg.font > 0) clip_line(wg.value_lbl, w - 16, vf);
}

static void build_button(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 120, h = j["h"] | 80;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_OFF));

    lv_obj_t *lbl = lv_label_create(wg.root);
    lv_obj_add_style(lbl, &s_value, LV_PART_MAIN);
    bool icon_only = wg.icon && !wg.title[0];
    const lv_font_t *bf = font_for(wg, icon_only && h >= 44 ? &ui_font_24 : &ui_font_16);
    lv_obj_set_style_text_font(lbl, bf, LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, w - 16 > 8 ? w - 16 : 8);
    if (wg.font > 0) clip_line(lbl, w - 16, bf);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    char buf[64];
    compose_text(wg, buf, sizeof(buf), wg.entity_id);
    lv_label_set_text(lbl, buf);
    lv_obj_center(lbl);

    wg.value_lbl = lbl;
    style_button(wg, false);
    lv_obj_add_flag(wg.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wg.root, cb_button_tap, LV_EVENT_CLICKED, &wg);
}

static void build_gauge(Widget &wg, JsonObjectConst j, bool interactive) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 150, h = j["h"] | 150;
    wg.root = make_card(x, y, w, h);
    make_title(wg, LV_ALIGN_TOP_MID);

    int arc_sz = (w < h ? w : h) - 24;
    if (arc_sz < 20) arc_sz = 20;
    wg.ctl = lv_arc_create(wg.root);
    lv_obj_set_size(wg.ctl, arc_sz, arc_sz);
    lv_arc_set_rotation(wg.ctl, 135);
    lv_arc_set_bg_angles(wg.ctl, 0, 270);
    lv_arc_set_range(wg.ctl, wg.min_val * wg.scale, wg.max_val * wg.scale);
    lv_arc_set_value(wg.ctl, wg.min_val * wg.scale);
    lv_obj_set_style_arc_color(wg.ctl, col_bg(wg, C_OFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(wg.ctl, col_on(wg), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wg.ctl, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wg.ctl, 6, LV_PART_MAIN);
    if (interactive) {
        lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_KNOB);
        lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_VALUE_CHANGED, &wg);
        lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_RELEASED, &wg);
    } else {
        lv_obj_remove_style(wg.ctl, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(wg.ctl, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_align(wg.ctl, LV_ALIGN_CENTER, 0, 6);

    make_value(wg, LV_ALIGN_CENTER, 0, 6, &ui_font_20);
    lv_obj_clear_flag(wg.value_lbl, LV_OBJ_FLAG_CLICKABLE);
}

static void build_switch(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | 60;
    wg.root = make_card(x, y, w, h);
    lv_obj_t *st = make_title(wg, LV_ALIGN_LEFT_MID);
    if (wg.font > 0) {
        lv_obj_set_style_text_font(st, font_for(wg, &ui_font_12), LV_PART_MAIN);
        clip_line(st, w - 16 - 56, font_for(wg, &ui_font_12));     // leave room for the switch itself
        lv_obj_align(st, LV_ALIGN_LEFT_MID, 0, 0);
    }

    wg.ctl = lv_switch_create(wg.root);
    style_track(wg, wg.ctl);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_align(wg.ctl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(wg.ctl, cb_switch_changed, LV_EVENT_VALUE_CHANGED, &wg);
}

static void build_checkbox(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | 40;
    wg.root = make_card(x, y, w, h);

    wg.ctl = lv_checkbox_create(wg.root);
    char buf[64];
    compose_text(wg, buf, sizeof(buf), wg.entity_id);
    lv_checkbox_set_text(wg.ctl, buf);              // copied by LVGL
    lv_obj_set_style_text_font(wg.ctl, font_for(wg, &ui_font_16), LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.ctl, col_fg(wg, C_WHITE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(wg.ctl, col_bg(wg, C_OFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_set_style_border_width(wg.ctl, 0, LV_PART_INDICATOR);
    lv_obj_align(wg.ctl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(wg.ctl, cb_switch_changed, LV_EVENT_VALUE_CHANGED, &wg);
}

static void build_bar(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 200, h = j["h"] | 50;
    wg.root = make_card(x, y, w, h);
    make_title(wg, LV_ALIGN_TOP_LEFT);

    wg.ctl = lv_bar_create(wg.root);
    lv_obj_set_size(wg.ctl, w > 30 ? w - 20 : 10, 12);
    lv_bar_set_range(wg.ctl, wg.min_val * wg.scale, wg.max_val * wg.scale);
    lv_bar_set_value(wg.ctl, wg.min_val * wg.scale, LV_ANIM_OFF);
    style_track(wg, wg.ctl);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_INDICATOR);
    lv_obj_align(wg.ctl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    make_value(wg, LV_ALIGN_TOP_RIGHT, 0, 0, &ui_font_14);
    lv_obj_set_style_text_color(wg.value_lbl, col_fg(wg, C_MUTED), LV_PART_MAIN);
}

static void build_slider(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 200, h = j["h"] | 60;
    wg.root = make_card(x, y, w, h);
    bool compact = h < 52;
    int inner = w - 16 > 20 ? w - 16 : 20;

    make_title(wg, compact ? LV_ALIGN_LEFT_MID : LV_ALIGN_TOP_LEFT);
    if (compact) {
        lv_label_set_long_mode(wg.title_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(wg.title_lbl, inner * 40 / 100);
    } else {
        make_value(wg, LV_ALIGN_TOP_RIGHT, 0, 0, &ui_font_14);
        lv_obj_set_style_text_color(wg.value_lbl, col_fg(wg, C_MUTED), LV_PART_MAIN);
    }

    wg.ctl = lv_slider_create(wg.root);
    lv_obj_set_size(wg.ctl, compact ? inner * 55 / 100 : inner - 8, 10);
    lv_slider_set_range(wg.ctl, wg.min_val * wg.scale, wg.max_val * wg.scale);
    lv_slider_set_value(wg.ctl, wg.min_val * wg.scale, LV_ANIM_OFF);
    style_track(wg, wg.ctl);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_KNOB);
    if (compact) lv_obj_align(wg.ctl, LV_ALIGN_RIGHT_MID, -4, 0);
    else         lv_obj_align(wg.ctl, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_VALUE_CHANGED, &wg);
    lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_RELEASED, &wg);
}

static void build_led(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 40, h = j["h"] | 40;
    wg.root = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(wg.root, x, y);
    lv_obj_set_size(wg.root, w, h);
    lv_obj_set_style_bg_opa(wg.root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wg.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wg.root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wg.root, LV_OBJ_FLAG_SCROLLABLE);

    int d = w < h ? w : h;
    wg.ctl = lv_obj_create(wg.root);
    lv_obj_set_size(wg.ctl, d, d);
    lv_obj_set_style_radius(wg.ctl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(wg.ctl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wg.ctl, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wg.ctl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wg.ctl, col_bg(wg, C_OFF), LV_PART_MAIN);
    lv_obj_clear_flag(wg.ctl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(wg.ctl, LV_ALIGN_LEFT_MID, 0, 0);

    if (wg.title[0] && w - d >= 36) {
        lv_obj_t *t = lv_label_create(wg.root);
        lv_obj_add_style(t, &s_unit, LV_PART_MAIN);
        lv_obj_set_style_text_color(t, col_fg(wg, C_WHITE), LV_PART_MAIN);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, w - d - 6);
        lv_label_set_text(t, wg.title);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, d + 6, 0);
        wg.title_lbl = t;
    }
}

static const lv_font_t *clock_font(const Widget &wg, int w, int h) {
    if (wg.font > 0) {                          // fixed size: the largest clock/UI font that is <= the request
        static const struct { int size; const lv_font_t *font; } F[] = {
            {48, &ui_clock_48}, {32, &ui_clock_32}, {24, &ui_font_24}, {20, &ui_font_20}, {16, &ui_font_16}, {14, &ui_font_14}};
        for (const auto &f : F)
            if (wg.font >= f.size) return f.font;
        return &ui_font_12;
    }
    int chars = wg.format == CF_TIME ? 5 : wg.format == CF_TIME_S ? 8 : wg.format == CF_DATE ? 10 : 12;
    struct Cand { const lv_font_t *font; int size; };
    static const Cand C[] = {{&ui_clock_48, 48}, {&ui_clock_32, 32}, {&ui_font_24, 24}, {&ui_font_20, 20}, {&ui_font_16, 16}};
    for (const Cand &c : C)
        if (chars * c.size * 62 / 100 <= w - 16 && c.size * 13 / 10 <= h - 8) return c.font;
    return &ui_font_14;
}

static void build_clock(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 200, h = j["h"] | 80;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_CARD));
    make_value(wg, LV_ALIGN_CENTER, 0, 0, clock_font(wg, w, h));
    wg.v1[0] = 0;
    refresh_clock(wg);
}

static void build_panel(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 100, h = j["h"] | 100;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_CARD));
    lv_obj_clear_flag(wg.root, LV_OBJ_FLAG_CLICKABLE);
    if (wg.title[0]) make_title(wg, LV_ALIGN_TOP_LEFT);
}

static void build_select(Widget &wg, JsonObjectConst j, bool roller) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | (roller ? 100 : 40);
    wg.ctl = roller ? lv_roller_create(lv_scr_act()) : lv_dropdown_create(lv_scr_act());
    wg.root = wg.ctl;
    lv_obj_set_pos(wg.ctl, x, y);
    lv_obj_set_width(wg.ctl, w);
    if (roller) lv_obj_set_height(wg.ctl, h);
    lv_obj_set_style_bg_color(wg.ctl, col_bg(wg, C_OFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.ctl, col_fg(wg, C_WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(wg.ctl, font_for(wg, &ui_font_16), LV_PART_MAIN);
    lv_obj_set_style_border_width(wg.ctl, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wg.ctl, 10, LV_PART_MAIN);
    if (roller) {
        lv_roller_set_options(wg.ctl, "--", LV_ROLLER_MODE_NORMAL);
        lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_SELECTED);
        lv_obj_set_style_text_color(wg.ctl, C_BG, LV_PART_SELECTED);
        lv_obj_set_style_text_font(wg.ctl, font_for(wg, &ui_font_16), LV_PART_SELECTED);
    } else {
        lv_dropdown_set_options(wg.ctl, "--");
        lv_obj_add_event_cb(wg.ctl, cb_dropdown_open, LV_EVENT_READY, &wg);
    }
    lv_obj_add_event_cb(wg.ctl, cb_option, LV_EVENT_VALUE_CHANGED, &wg);
}

// Round-ish button with a symbol; used by media and thermostat.
static lv_obj_t *make_zone(Widget &wg, lv_obj_t *parent, int w, int h, lv_align_t align, int dx, int dy,
                           const char *text, const lv_font_t *font, lv_obj_t **label_out = nullptr) {
    lv_obj_t *z = lv_obj_create(parent);
    lv_obj_set_size(z, w, h);
    lv_obj_align(z, align, dx, dy);
    lv_obj_set_style_bg_color(z, col_bg(wg, C_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(z, col_on(wg), (lv_style_selector_t)(LV_PART_MAIN | LV_STATE_PRESSED));
    lv_obj_set_style_border_width(z, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(z, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(z, 0, LV_PART_MAIN);
    lv_obj_clear_flag(z, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z, cb_act, LV_EVENT_CLICKED, &wg);

    lv_obj_t *l = lv_label_create(z);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, col_fg(wg, C_WHITE), LV_PART_MAIN);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (label_out) *label_out = l;
    return z;
}

static void build_media(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 240, h = j["h"] | 120;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_CARD));
    int inner = w - 16 > 40 ? w - 16 : 40;
    int ch = h - 16;

    wg.title_lbl = lv_label_create(wg.root);
    const lv_font_t *mf = font_for(wg, &ui_font_16);
    lv_obj_set_style_text_font(wg.title_lbl, mf, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.title_lbl, col_fg(wg, C_WHITE), LV_PART_MAIN);
    lv_label_set_long_mode(wg.title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wg.title_lbl, inner);
    if (wg.font > 0) clip_line(wg.title_lbl, inner, mf);
    lv_label_set_text(wg.title_lbl, wg.title[0] ? wg.title : "--");
    lv_obj_align(wg.title_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    wg.aux_lbl = lv_label_create(wg.root);
    lv_obj_set_style_text_font(wg.aux_lbl, &ui_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.aux_lbl, col_fg(wg, C_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(wg.aux_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wg.aux_lbl, inner);
    lv_label_set_text(wg.aux_lbl, "");
    lv_obj_align(wg.aux_lbl, LV_ALIGN_TOP_LEFT, 0, 20);

    int middle = ch - 36 - 24;
    int bh = middle < 24 ? 24 : (middle > 44 ? 44 : middle);
    int by = 36 + (middle - bh) / 2;
    if (by < 36) by = 36;
    int bw = (inner - 16) / 3;
    if (bw > 56) bw = 56;
    if (bw < 24) bw = 24;
    int gap = (inner - 3 * bw) / 2;
    if (gap < 0) gap = 0;
    int x0 = (inner - 3 * bw - 2 * gap) / 2;
    if (x0 < 0) x0 = 0;
    wg.aux1 = make_zone(wg, wg.root, bw, bh, LV_ALIGN_TOP_LEFT, x0, by, LV_SYMBOL_PREV, &ui_font_16);
    wg.aux2 = make_zone(wg, wg.root, bw, bh, LV_ALIGN_TOP_LEFT, x0 + bw + gap, by, LV_SYMBOL_PLAY, &ui_font_16, &wg.aux_lbl2);
    wg.aux3 = make_zone(wg, wg.root, bw, bh, LV_ALIGN_TOP_LEFT, x0 + 2 * (bw + gap), by, LV_SYMBOL_NEXT, &ui_font_16);

    wg.ctl = lv_slider_create(wg.root);
    lv_obj_set_size(wg.ctl, inner - 12, 8);
    lv_slider_set_range(wg.ctl, 0, 100);
    lv_slider_set_value(wg.ctl, 0, LV_ANIM_OFF);
    style_track(wg, wg.ctl);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(wg.ctl, col_on(wg), LV_PART_KNOB);
    lv_obj_align(wg.ctl, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_VALUE_CHANGED, &wg);
    lv_obj_add_event_cb(wg.ctl, cb_value, LV_EVENT_RELEASED, &wg);
}

static void build_thermostat(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 200, h = j["h"] | 110;
    wg.root = make_card(x, y, w, h, col_bg(wg, C_CARD));
    int inner = w - 16 > 40 ? w - 16 : 40;
    int bs = h - 16 - 28;                      // button size: leave room for the title row and the mode line
    if (bs > 44) bs = 44;
    if (bs < 24) bs = 24;

    make_title(wg, LV_ALIGN_TOP_LEFT);
    lv_label_set_long_mode(wg.title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wg.title_lbl, inner / 2);

    wg.aux_lbl = lv_label_create(wg.root);               // current temperature, top right
    lv_obj_set_style_text_font(wg.aux_lbl, &ui_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.aux_lbl, col_fg(wg, C_MUTED), LV_PART_MAIN);
    lv_label_set_text(wg.aux_lbl, "--");
    lv_obj_align(wg.aux_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    wg.aux1 = make_zone(wg, wg.root, bs, bs, LV_ALIGN_LEFT_MID, 0, 4, LV_SYMBOL_MINUS, &ui_font_20);
    wg.aux2 = make_zone(wg, wg.root, bs, bs, LV_ALIGN_RIGHT_MID, 0, 4, LV_SYMBOL_PLUS, &ui_font_20);

    const lv_font_t *tf = font_for(wg, h >= 80 ? &ui_font_24 : &ui_font_20);
    make_value(wg, LV_ALIGN_CENTER, 0, 4, tf);
    if (wg.font > 0) {                                   // keep it between the two buttons
        clip_line(wg.value_lbl, w - 2 * (bs + 8) - 8, tf);
        lv_obj_set_style_text_align(wg.value_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(wg.value_lbl, LV_ALIGN_CENTER, 0, 4);
    }

    wg.aux_lbl2 = lv_label_create(wg.root);              // mode text, bottom centre
    lv_obj_set_style_text_font(wg.aux_lbl2, &ui_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.aux_lbl2, col_fg(wg, C_MUTED), LV_PART_MAIN);
    lv_label_set_text(wg.aux_lbl2, "");
    lv_obj_align(wg.aux_lbl2, LV_ALIGN_BOTTOM_MID, 0, 0);
}

// ── Page switcher ─────────────────────────────────────────────────────────────

static void cb_page(lv_event_t *e) {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 250) return;   // a double tap must not skip a page
    last = now;
    ha_client_page((int)(intptr_t)lv_event_get_user_data(e));
}

static void pager_zone(lv_obj_t *card, int w, int h, bool left, const char *symbol, const lv_font_t *font) {
    lv_obj_t *z = lv_obj_create(card);
    lv_obj_set_size(z, w, h);
    lv_obj_align(z, left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(z, C_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_color(z, C_ACCENT, (lv_style_selector_t)(LV_PART_MAIN | LV_STATE_PRESSED));
    lv_obj_set_style_border_width(z, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(z, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(z, 0, LV_PART_MAIN);
    lv_obj_clear_flag(z, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z, cb_page, LV_EVENT_CLICKED, (void *)(intptr_t)(left ? -1 : 1));

    lv_obj_t *l = lv_label_create(z);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, C_WHITE, LV_PART_MAIN);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
}

// "<  Name  >" at the position and size chosen in the layout. Built last, so it lies above the widgets.
static void build_pager(JsonObjectConst pg, JsonObjectConst page) {
    int x = pg["x"] | 0, y = pg["y"] | 0, w = pg["w"] | 200, h = pg["h"] | 44;
    if (w < 72) w = 72;
    if (h < 24) h = 24;

    int index = page["index"] | 0, count = page["count"] | 1;
    char text[48];
    const char *name = page["name"] | "";
    if (strcmp(pg["mode"] | "name", "count") == 0 || !name[0]) snprintf(text, sizeof(text), "%d / %d", index + 1, count);
    else                                                        copy_utf8(text, name, sizeof(text));

    lv_obj_t *card = make_card(x, y, w, h);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);

    int aw = (int)(h * 1.2f);
    if (aw > w / 3) aw = w / 3;
    if (aw < 24) aw = 24;
    const lv_font_t *arrow = h >= 30 ? &ui_font_24 : &ui_font_16;
    pager_zone(card, aw, h, true,  "<", arrow);
    pager_zone(card, aw, h, false, ">", arrow);

    const lv_font_t *font = h >= 48 ? &ui_font_20 : h >= 32 ? &ui_font_16 : &ui_font_14;
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, w - 2 * aw - 8);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

// ── Refresh a widget from its stored values ───────────────────────────────────

static int find_option(const char *opts, const char *value) {
    if (!opts || !value || !value[0]) return -1;
    size_t vl = strlen(value);
    int index = 0;
    const char *p = opts;
    while (true) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len == vl && strncmp(p, value, vl) == 0) return index;
        if (!nl) return -1;
        p = nl + 1;
        index++;
    }
}

static void refresh_select(Widget &wg) {
    if (!wg.ctl) return;
    const char *opts = wg.type == WT_ROLLER ? lv_roller_get_options(wg.ctl) : lv_dropdown_get_options(wg.ctl);
    int idx = find_option(opts, wg.v1);
    bool free_to_move = millis() >= wg.lock_until;
    if (wg.type == WT_ROLLER) {
        if (idx >= 0 && free_to_move) lv_roller_set_selected(wg.ctl, (uint16_t)idx, LV_ANIM_OFF);
    } else {
        if (idx >= 0) {
            if (free_to_move) lv_dropdown_set_selected(wg.ctl, (uint16_t)idx);
            lv_dropdown_set_text(wg.ctl, NULL);                       // closed button shows the selected option
        } else {
            lv_dropdown_set_text(wg.ctl, wg.v1[0] ? wg.v1 : "--");    // wg.v1 lives as long as the widget
        }
        lv_obj_invalidate(wg.ctl);
    }
}

static void temp_text(const Widget &wg, const char *value, char *out, size_t n) {
    if (!value[0]) { strlcpy(out, "--", n); return; }
    bool tight = !wg.unit[0] || wg.unit[0] == '%' || ((uint8_t)wg.unit[0] == 0xC2 && (uint8_t)wg.unit[1] == 0xB0);
    snprintf(out, n, "%s%s%s", value, tight ? "" : " ", wg.unit[0] ? wg.unit : "\xC2\xB0");
}

static void refresh(Widget &wg) {
    float v = 0;
    switch (wg.type) {
    case WT_LABEL:
        if (wg.value_lbl) set_value_text(wg, main_value(wg));
        break;
    case WT_BUTTON:
        style_button(wg, is_active(wg.state));
        break;
    case WT_GAUGE:
    case WT_BAR: {
        bool ok = parse_num(main_value(wg), v);
        int raw = ok ? (int)lroundf(v * wg.scale) : wg.min_val * wg.scale;
        if (wg.type == WT_GAUGE) {
            lv_arc_set_value(wg.ctl, raw);
            lv_obj_set_style_arc_color(wg.ctl, indicator_color(wg, ok, v), LV_PART_INDICATOR);
        } else {
            lv_bar_set_value(wg.ctl, raw, LV_ANIM_ON);
            lv_obj_set_style_bg_color(wg.ctl, indicator_color(wg, ok, v), LV_PART_INDICATOR);
        }
        if (ok) set_value_text(wg, main_value(wg));
        else    lv_label_set_text(wg.value_lbl, "--");
        break;
    }
    case WT_SWITCH:
    case WT_CHECKBOX:
        if (is_active(wg.state)) lv_obj_add_state(wg.ctl, LV_STATE_CHECKED);
        else                     lv_obj_clear_state(wg.ctl, LV_STATE_CHECKED);
        break;
    case WT_SLIDER:
    case WT_ARC: {
        bool ok = parse_num(wg.v1, v);
        if (value_locked(wg, wg.ctl)) break;
        if (ok) {
            int raw = (int)lroundf(v * wg.scale);
            if (wg.type == WT_ARC) lv_arc_set_value(wg.ctl, raw);
            else                   lv_slider_set_value(wg.ctl, raw, LV_ANIM_OFF);
        }
        if (wg.value_lbl) {
            if (ok) set_value_text(wg, wg.v1);
            else    lv_label_set_text(wg.value_lbl, "--");
        }
        break;
    }
    case WT_LED:
        lv_obj_set_style_bg_color(wg.ctl, is_active(wg.state) ? col_on(wg) : col_bg(wg, C_OFF), LV_PART_MAIN);
        break;
    case WT_DROPDOWN:
    case WT_ROLLER:
        refresh_select(wg);
        break;
    case WT_CLOCK:
    case WT_PANEL:
        break;
    case WT_MEDIA: {
        lv_label_set_text(wg.title_lbl, wg.v1[0] ? wg.v1 : (wg.title[0] ? wg.title : "--"));
        lv_label_set_text(wg.aux_lbl, wg.v2);
        bool playing = strcasecmp(wg.state, "playing") == 0;
        lv_label_set_text(wg.aux_lbl2, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_obj_set_style_bg_color(wg.aux2, playing ? col_on(wg) : col_bg(wg, C_OFF), LV_PART_MAIN);
        lv_obj_set_style_text_color(wg.aux_lbl2, playing && !wg.has_fg ? C_BG : col_fg(wg, C_WHITE), LV_PART_MAIN);
        if (parse_num(wg.v3, v) && !value_locked(wg, wg.ctl)) lv_slider_set_value(wg.ctl, (int)lroundf(v), LV_ANIM_OFF);
        break;
    }
    case WT_THERMOSTAT: {
        char buf[40];
        temp_text(wg, wg.v2, buf, sizeof(buf));
        lv_label_set_text(wg.value_lbl, buf);
        temp_text(wg, wg.v1, buf, sizeof(buf));
        lv_label_set_text(wg.aux_lbl, buf);
        lv_label_set_text(wg.aux_lbl2, wg.state);
        lv_obj_set_style_text_color(wg.aux_lbl2, is_active(wg.state) ? col_on(wg) : col_fg(wg, C_MUTED), LV_PART_MAIN);
        break;
    }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void widgets_reset() {
    memset(_w, 0, sizeof(_w));
    _count = 0;
}

static WidgetType type_of(const char *type) {
    static const struct { const char *name; WidgetType type; } T[] = {
        {"button", WT_BUTTON}, {"gauge", WT_GAUGE}, {"switch", WT_SWITCH}, {"bar", WT_BAR}, {"slider", WT_SLIDER},
        {"arc", WT_ARC}, {"led", WT_LED}, {"clock", WT_CLOCK}, {"dropdown", WT_DROPDOWN}, {"roller", WT_ROLLER},
        {"checkbox", WT_CHECKBOX}, {"panel", WT_PANEL}, {"media", WT_MEDIA}, {"thermostat", WT_THERMOSTAT}};
    for (const auto &t : T) if (strcmp(type, t.name) == 0) return t.type;
    return WT_LABEL;
}

static void parse_widget(Widget &wg, JsonObjectConst j) {
    copy_utf8(wg.id,        j["id"]     | "", sizeof(wg.id));
    copy_utf8(wg.entity_id, j["entity"] | "", sizeof(wg.entity_id));
    copy_utf8(wg.unit,      j["unit"]   | "", sizeof(wg.unit));
    copy_utf8(wg.title,     j["label"]  | "", sizeof(wg.title));
    copy_utf8(wg.attr,      j["attr"]   | "", sizeof(wg.attr));
    wg.type    = type_of(j["type"] | "label");
    wg.min_val = j["min"] | 0;
    wg.max_val = j["max"] | 100;
    if (wg.max_val <= wg.min_val) wg.max_val = wg.min_val + 1;
    wg.scale   = scale_of(wg.min_val, wg.max_val);
    wg.has_bg  = parse_color(j["bg"] | "", wg.bg);
    wg.has_fg  = parse_color(j["fg"] | "", wg.fg);
    wg.has_on  = parse_color(j["on"] | "", wg.on);
    wg.has_warn = !j["warn"].isNull();
    wg.has_crit = !j["crit"].isNull();
    wg.warn    = j["warn"] | 0.0f;
    wg.crit    = j["crit"] | 0.0f;
    wg.invert  = j["invert"] | false;
    wg.icon    = icon_symbol(j["icon"] | "");
    int fnt    = j["font"] | 0;
    wg.font    = fnt > 0 ? fnt : 0;               // unknown or negative: automatic
    const char *fmt = j["format"] | "time";
    wg.format  = strcmp(fmt, "time_s") == 0 ? CF_TIME_S : strcmp(fmt, "date") == 0 ? CF_DATE
               : strcmp(fmt, "datetime") == 0 ? CF_DATETIME : CF_TIME;
}

int widgets_build(JsonObjectConst frame, const char *device_name) {
    init_styles();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);                 // frees the objects with their local styles, option lists and dropdown lists
    widgets_reset();
    lv_color_t page_bg = C_BG;
    parse_color(frame["page"]["bg"] | "", page_bg);
    lv_obj_set_style_bg_color(scr, page_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    for (JsonObjectConst j : frame["widgets"].as<JsonArrayConst>()) {
        if (_count >= MAX_WIDGETS) break;

        Widget &wg = _w[_count];
        parse_widget(wg, j);

        switch (wg.type) {
        case WT_BUTTON:     build_button(wg, j);        break;
        case WT_GAUGE:      build_gauge(wg, j, false);  break;
        case WT_ARC:        build_gauge(wg, j, true);   break;
        case WT_SWITCH:     build_switch(wg, j);        break;
        case WT_CHECKBOX:   build_checkbox(wg, j);      break;
        case WT_BAR:        build_bar(wg, j);           break;
        case WT_SLIDER:     build_slider(wg, j);        break;
        case WT_LED:        build_led(wg, j);           break;
        case WT_CLOCK:      build_clock(wg, j);         break;
        case WT_DROPDOWN:   build_select(wg, j, false); break;
        case WT_ROLLER:     build_select(wg, j, true);  break;
        case WT_PANEL:      build_panel(wg, j);         break;
        case WT_MEDIA:      build_media(wg, j);         break;
        case WT_THERMOSTAT: build_thermostat(wg, j);    break;
        default:            build_label(wg, j);         break;
        }
        if (wg.type == WT_THERMOSTAT || wg.type == WT_MEDIA) refresh(wg);   // fill in the placeholders

        _count++;
    }

    if (_count == 0) {
        int w = lv_disp_get_hor_res(NULL) - 40;
        lv_obj_t *name = lv_label_create(scr);
        lv_obj_set_style_text_font(name, &ui_font_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(name, C_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name, w);
        lv_label_set_text(name, device_name);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, -30);

        lv_obj_t *hint = lv_label_create(scr);
        lv_obj_set_style_text_font(hint, &ui_font_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(hint, C_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, w);
        lv_label_set_text(hint, "Keine Elemente auf dieser Seite \xE2\x80\x93 in Home Assistant konfigurieren");
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 20);
    }

    JsonObjectConst pager = frame["pager"].as<JsonObjectConst>();
    if (!pager.isNull()) build_pager(pager, frame["page"].as<JsonObjectConst>());

    Serial.printf("[UI] built %d widgets%s\n", _count, pager.isNull() ? "" : " + pager");
    return _count;
}

// KEY is "entity" or "entity|name"; which field of a widget a name feeds depends on its type (protocol "States").
static char *field_for(Widget &wg, const char *name, size_t &size) {
    if (!name[0]) { size = sizeof(wg.state); return wg.state; }
    switch (wg.type) {
    case WT_SLIDER: case WT_ARC:
        if (strcmp(name, "value") == 0) { size = sizeof(wg.v1); return wg.v1; }
        break;
    case WT_DROPDOWN: case WT_ROLLER:
        if (strcmp(name, "option") == 0) { size = sizeof(wg.v1); return wg.v1; }
        break;
    case WT_MEDIA:
        if (strcmp(name, "media_title") == 0)  { size = sizeof(wg.v1); return wg.v1; }
        if (strcmp(name, "media_artist") == 0) { size = sizeof(wg.v2); return wg.v2; }
        if (strcmp(name, "value") == 0)        { size = sizeof(wg.v3); return wg.v3; }
        break;
    case WT_THERMOSTAT:
        if (strcmp(name, "current_temperature") == 0) { size = sizeof(wg.v1); return wg.v1; }
        if (strcmp(name, "temperature") == 0)         { size = sizeof(wg.v2); return wg.v2; }
        break;
    case WT_CLOCK: case WT_PANEL: case WT_SWITCH: case WT_CHECKBOX: case WT_LED:
        break;
    default:
        if (wg.attr[0] && strcmp(name, wg.attr) == 0) { size = sizeof(wg.v1); return wg.v1; }
        break;
    }
    return nullptr;
}

void widgets_update(const char *key, const char *state) {
    if (!key || !state) return;
    char entity[64];
    const char *bar = strchr(key, '|');
    const char *name = bar ? bar + 1 : "";
    size_t elen = bar ? (size_t)(bar - key) : strlen(key);
    if (elen >= sizeof(entity)) return;
    memcpy(entity, key, elen);
    entity[elen] = 0;

    for (int i = 0; i < _count; i++) {
        Widget &wg = _w[i];
        if (!wg.entity_id[0] || strcmp(wg.entity_id, entity) != 0) continue;
        size_t size = 0;
        char *field = field_for(wg, name, size);
        if (!field) continue;
        strlcpy(field, state, size);
        refresh(wg);
    }
}

void widgets_options(JsonObjectConst frame) {
    const char *id = frame["widget"] | "";
    JsonArrayConst items = frame["items"].as<JsonArrayConst>();
    if (!id[0] || items.isNull()) return;
    for (int i = 0; i < _count; i++) {
        Widget &wg = _w[i];
        if ((wg.type != WT_DROPDOWN && wg.type != WT_ROLLER) || strcmp(wg.id, id) != 0 || !wg.ctl) continue;
        String joined;
        int n = 0;
        for (JsonVariantConst item : items) {
            if (n >= 16) break;
            char one[32];
            copy_utf8(one, item | "", sizeof(one));
            if (n) joined += '\n';
            joined += one;
            n++;
        }
        if (!n) joined = "--";
        if (wg.type == WT_ROLLER) lv_roller_set_options(wg.ctl, joined.c_str(), LV_ROLLER_MODE_NORMAL);  // copied
        else                      lv_dropdown_set_options(wg.ctl, joined.c_str());                         // copied
        refresh_select(wg);
    }
}

void widgets_time(JsonObjectConst frame) {
    _t_epoch  = frame["epoch"] | (int64_t)0;
    _t_offset = frame["offset"] | 0;
    _t_millis = millis();
    _t_have   = _t_epoch > 0;
    for (int i = 0; i < _count; i++) if (_w[i].type == WT_CLOCK) refresh_clock(_w[i]);
}

void widgets_tick() {
    uint32_t now = millis();
    if (now - _t_last_tick < 250) return;
    _t_last_tick = now;
    for (int i = 0; i < _count; i++) if (_w[i].type == WT_CLOCK) refresh_clock(_w[i]);
}
