#include "widgets.h"
#include "ha_client.h"
#include "fonts.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

// ── Types & storage ───────────────────────────────────────────────────────────

enum WidgetType { WT_LABEL, WT_BUTTON, WT_GAUGE, WT_SWITCH, WT_BAR };

struct Widget {
    char       id[16];
    WidgetType type;
    char       entity_id[64];
    char       unit[16];
    char       title[32];
    int        min_val, max_val;
    char       state[48];
    // LVGL handles
    lv_obj_t  *root;
    lv_obj_t  *value_lbl;
    lv_obj_t  *arc;
    lv_obj_t  *bar;
    lv_obj_t  *sw;
};

static Widget _w[32];
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

// ── Helper: make a card container ─────────────────────────────────────────────

static lv_obj_t *make_card(int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(obj, &s_card, LV_PART_MAIN);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
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

static void set_value_text(Widget &wg, const char *state) {
    char buf[64];
    bool tight = wg.unit[0] == '%' || ((uint8_t)wg.unit[0] == 0xC2 && (uint8_t)wg.unit[1] == 0xB0);  // "%", "°C"
    if (wg.unit[0]) snprintf(buf, sizeof(buf), "%s%s%s", state, tight ? "" : " ", wg.unit);
    else            strlcpy(buf, state, sizeof(buf));
    lv_label_set_text(wg.value_lbl, buf);
}

// ── Touch callbacks ───────────────────────────────────────────────────────────

static void style_button(Widget &wg, bool on) {
    lv_obj_set_style_bg_color(wg.root, on ? C_ON : C_OFF, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.value_lbl, on ? C_BG : C_WHITE, LV_PART_MAIN);
}

static void cb_button_tap(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0]) return;
    ha_client_tap(wg->id);
    // Optimistic flip for toggling entities; the states frame corrects it.
    if (strncmp(wg->entity_id, "button.", 7) && strncmp(wg->entity_id, "input_button.", 13) &&
        strncmp(wg->entity_id, "scene.", 6) && strncmp(wg->entity_id, "script.", 7))
        style_button(*wg, strcmp(wg->state, "on") != 0);
}

static void cb_switch_changed(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    if (!wg->entity_id[0]) return;
    ha_client_set(wg->id, lv_obj_has_state(wg->sw, LV_STATE_CHECKED));
}

// ── Widget builders ───────────────────────────────────────────────────────────

static void build_label(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | 70;
    wg.root = make_card(x, y, w, h);

    lv_obj_t *title = lv_label_create(wg.root);
    lv_obj_add_style(title, &s_title, LV_PART_MAIN);
    lv_label_set_text(title, wg.title[0] ? wg.title : wg.entity_id);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    wg.value_lbl = lv_label_create(wg.root);
    lv_obj_add_style(wg.value_lbl, &s_value, LV_PART_MAIN);
    lv_label_set_text(wg.value_lbl, "--");
    lv_obj_align(wg.value_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void build_button(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 120, h = j["h"] | 80;
    wg.root = make_card(x, y, w, h);
    lv_obj_set_style_bg_color(wg.root, C_OFF, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(wg.root);
    lv_obj_add_style(lbl, &s_value, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &ui_font_16, LV_PART_MAIN);
    lv_label_set_text(lbl, wg.title[0] ? wg.title : wg.entity_id);
    lv_obj_center(lbl);

    wg.value_lbl = lbl;
    lv_obj_add_flag(wg.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wg.root, cb_button_tap, LV_EVENT_CLICKED, &wg);
}

static void build_gauge(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 150, h = j["h"] | 150;
    wg.root = make_card(x, y, w, h);

    lv_obj_t *title = lv_label_create(wg.root);
    lv_obj_add_style(title, &s_title, LV_PART_MAIN);
    lv_label_set_text(title, wg.title[0] ? wg.title : wg.entity_id);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    int arc_sz = (w < h ? w : h) - 24;
    if (arc_sz < 20) arc_sz = 20;
    wg.arc = lv_arc_create(wg.root);
    lv_obj_set_size(wg.arc, arc_sz, arc_sz);
    lv_arc_set_rotation(wg.arc, 135);
    lv_arc_set_bg_angles(wg.arc, 0, 270);
    lv_arc_set_range(wg.arc, wg.min_val, wg.max_val);
    lv_arc_set_value(wg.arc, wg.min_val);
    lv_obj_remove_style(wg.arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(wg.arc, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wg.arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wg.arc, 6, LV_PART_MAIN);
    lv_obj_clear_flag(wg.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(wg.arc, LV_ALIGN_CENTER, 0, 6);

    wg.value_lbl = lv_label_create(wg.root);
    lv_obj_add_style(wg.value_lbl, &s_value, LV_PART_MAIN);
    lv_label_set_text(wg.value_lbl, "--");
    lv_obj_align(wg.value_lbl, LV_ALIGN_CENTER, 0, 6);
}

static void build_switch(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 160, h = j["h"] | 60;
    wg.root = make_card(x, y, w, h);

    lv_obj_t *title = lv_label_create(wg.root);
    lv_obj_add_style(title, &s_title, LV_PART_MAIN);
    lv_label_set_text(title, wg.title[0] ? wg.title : wg.entity_id);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    wg.sw = lv_switch_create(wg.root);
    lv_obj_set_style_bg_color(wg.sw, C_ACCENT, (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_align(wg.sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(wg.sw, cb_switch_changed, LV_EVENT_VALUE_CHANGED, &wg);
}

static void build_bar(Widget &wg, JsonObjectConst j) {
    int x = j["x"] | 0, y = j["y"] | 0, w = j["w"] | 200, h = j["h"] | 50;
    wg.root = make_card(x, y, w, h);

    lv_obj_t *title = lv_label_create(wg.root);
    lv_obj_add_style(title, &s_title, LV_PART_MAIN);
    lv_label_set_text(title, wg.title[0] ? wg.title : wg.entity_id);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    wg.bar = lv_bar_create(wg.root);
    lv_obj_set_size(wg.bar, w > 30 ? w - 20 : 10, 12);
    lv_bar_set_range(wg.bar, wg.min_val, wg.max_val);
    lv_bar_set_value(wg.bar, wg.min_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wg.bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(wg.bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    wg.value_lbl = lv_label_create(wg.root);
    lv_obj_add_style(wg.value_lbl, &s_unit, LV_PART_MAIN);
    lv_label_set_text(wg.value_lbl, "--");
    lv_obj_align(wg.value_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);
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

// ── State update helpers ──────────────────────────────────────────────────────

static void update_label(Widget &wg, const char *state) {
    set_value_text(wg, state);
}

static void update_button(Widget &wg, const char *state) {
    style_button(wg, strcmp(state, "on") == 0);
}

static void update_gauge(Widget &wg, const char *state) {
    float v;
    bool ok = parse_num(state, v);
    lv_arc_set_value(wg.arc, ok ? (int)lroundf(v) : wg.min_val);
    if (ok) set_value_text(wg, state);
    else    lv_label_set_text(wg.value_lbl, "--");
}

static void update_switch(Widget &wg, const char *state) {
    if (strcmp(state, "on") == 0) lv_obj_add_state(wg.sw, LV_STATE_CHECKED);
    else                          lv_obj_clear_state(wg.sw, LV_STATE_CHECKED);
}

static void update_bar(Widget &wg, const char *state) {
    float v;
    bool ok = parse_num(state, v);
    lv_bar_set_value(wg.bar, ok ? (int)lroundf(v) : wg.min_val, LV_ANIM_ON);
    if (ok) set_value_text(wg, state);
    else    lv_label_set_text(wg.value_lbl, "--");
}

// ── Public API ────────────────────────────────────────────────────────────────

void widgets_reset() {
    memset(_w, 0, sizeof(_w));
    _count = 0;
}

int widgets_build(JsonObjectConst frame, const char *device_name) {
    init_styles();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    widgets_reset();
    lv_obj_set_style_bg_color(scr, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    for (JsonObjectConst j : frame["widgets"].as<JsonArrayConst>()) {
        if (_count >= 32) break;

        Widget &wg = _w[_count];
        copy_utf8(wg.id,        j["id"]     | "", sizeof(wg.id));
        copy_utf8(wg.entity_id, j["entity"] | "", sizeof(wg.entity_id));
        copy_utf8(wg.unit,      j["unit"]   | "", sizeof(wg.unit));
        copy_utf8(wg.title,     j["label"]  | "", sizeof(wg.title));
        wg.min_val = j["min"] | 0;
        wg.max_val = j["max"] | 100;
        if (wg.max_val <= wg.min_val) wg.max_val = wg.min_val + 1;
        wg.root    = nullptr;

        const char *type = j["type"] | "label";
        if      (strcmp(type, "button") == 0) { wg.type = WT_BUTTON; build_button(wg, j); }
        else if (strcmp(type, "gauge")  == 0) { wg.type = WT_GAUGE;  build_gauge(wg, j);  }
        else if (strcmp(type, "switch") == 0) { wg.type = WT_SWITCH; build_switch(wg, j); }
        else if (strcmp(type, "bar")    == 0) { wg.type = WT_BAR;    build_bar(wg, j);    }
        else                                  { wg.type = WT_LABEL;  build_label(wg, j);  }

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

void widgets_update(const char *entity_id, const char *state) {
    for (int i = 0; i < _count; i++) {
        Widget &wg = _w[i];
        if (strcmp(wg.entity_id, entity_id) != 0) continue;
        strlcpy(wg.state, state, sizeof(wg.state));

        switch (wg.type) {
        case WT_LABEL:  update_label(wg, state);  break;
        case WT_BUTTON: update_button(wg, state); break;
        case WT_GAUGE:  update_gauge(wg, state);  break;
        case WT_SWITCH: update_switch(wg, state); break;
        case WT_BAR:    update_bar(wg, state);    break;
        }
    }
}
