#include "widgets.h"
#include "ha_client.h"
#include <cstring>
#include <cstdlib>

// ── Types & storage ───────────────────────────────────────────────────────────

enum WidgetType { WT_LABEL, WT_BUTTON, WT_GAUGE, WT_SWITCH, WT_BAR };

struct Widget {
    char       id[16];
    WidgetType type;
    char       entity_id[64];
    char       unit[16];
    char       title[32];
    char       service_domain[32];
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
    lv_style_set_text_font(&s_title, &lv_font_montserrat_12);

    lv_style_init(&s_value);
    lv_style_set_text_color(&s_value, C_WHITE);
    lv_style_set_text_font(&s_value, &lv_font_montserrat_20);

    lv_style_init(&s_unit);
    lv_style_set_text_color(&s_unit, C_MUTED);
    lv_style_set_text_font(&s_unit, &lv_font_montserrat_14);

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

// ── Touch callbacks ───────────────────────────────────────────────────────────

static void cb_button_tap(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    const char *domain  = wg->service_domain[0] ? wg->service_domain : "homeassistant";
    const char *service = (strcmp(wg->state, "on") == 0) ? "turn_off" : "turn_on";
    ha_client_call_service(domain, service, wg->entity_id);
    // Optimistic UI: flip color immediately
    bool now_on = (strcmp(wg->state, "off") == 0);
    lv_obj_set_style_bg_color(wg->root, now_on ? C_ON : C_OFF, LV_PART_MAIN);
}

static void cb_switch_changed(lv_event_t *e) {
    Widget *wg = (Widget *)lv_event_get_user_data(e);
    bool checked = lv_obj_has_state(wg->sw, LV_STATE_CHECKED);
    const char *domain = wg->service_domain[0] ? wg->service_domain : "homeassistant";
    ha_client_call_service(domain, checked ? "turn_on" : "turn_off", wg->entity_id);
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
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(wg.sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
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
    lv_obj_set_size(wg.bar, w - 20, 12);
    lv_bar_set_range(wg.bar, wg.min_val, wg.max_val);
    lv_bar_set_value(wg.bar, wg.min_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wg.bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(wg.bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    wg.value_lbl = lv_label_create(wg.root);
    lv_obj_add_style(wg.value_lbl, &s_unit, LV_PART_MAIN);
    lv_label_set_text(wg.value_lbl, "--");
    lv_obj_align(wg.value_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);
}

// ── State update helpers ──────────────────────────────────────────────────────

static void update_label(Widget &wg, const char *state) {
    char buf[48];
    if (wg.unit[0])
        snprintf(buf, sizeof(buf), "%s %s", state, wg.unit);
    else
        strlcpy(buf, state, sizeof(buf));
    lv_label_set_text(wg.value_lbl, buf);
}

static void update_button(Widget &wg, const char *state) {
    bool on = (strcmp(state, "on") == 0);
    lv_obj_set_style_bg_color(wg.root, on ? C_ON : C_OFF, LV_PART_MAIN);
    lv_obj_set_style_text_color(wg.value_lbl,
                                on ? C_BG : C_WHITE, LV_PART_MAIN);
}

static void update_gauge(Widget &wg, const char *state) {
    float v = atof(state);
    int   iv = (int)v;
    lv_arc_set_value(wg.arc, iv);
    char buf[32];
    if (wg.unit[0]) snprintf(buf, sizeof(buf), "%.1f%s", v, wg.unit);
    else             snprintf(buf, sizeof(buf), "%.1f", v);
    lv_label_set_text(wg.value_lbl, buf);
}

static void update_switch(Widget &wg, const char *state) {
    bool on = (strcmp(state, "on") == 0);
    if (on) lv_obj_add_state(wg.sw, LV_STATE_CHECKED);
    else    lv_obj_clear_state(wg.sw, LV_STATE_CHECKED);
}

static void update_bar(Widget &wg, const char *state) {
    float v = atof(state);
    lv_bar_set_value(wg.bar, (int)v, LV_ANIM_ON);
    char buf[32];
    if (wg.unit[0]) snprintf(buf, sizeof(buf), "%.0f%s", v, wg.unit);
    else             snprintf(buf, sizeof(buf), "%.0f", v);
    lv_label_set_text(wg.value_lbl, buf);
}

// ── Public API ────────────────────────────────────────────────────────────────

void widgets_clear() {
    // Deleting root deletes all children
    for (int i = 0; i < _count; i++) {
        if (_w[i].root) lv_obj_del(_w[i].root);
    }
    memset(_w, 0, sizeof(_w));
    _count = 0;
}

void widgets_build(JsonArrayConst layout) {
    widgets_clear();
    init_styles();

    // Dark background
    lv_obj_set_style_bg_color(lv_scr_act(), C_BG, LV_PART_MAIN);

    for (JsonObjectConst j : layout) {
        if (_count >= 32) break;

        Widget &wg = _w[_count];
        strlcpy(wg.id,             j["id"]     | "", 16);
        strlcpy(wg.entity_id,      j["entity"] | "", 64);
        strlcpy(wg.unit,           j["unit"]   | "", 16);
        strlcpy(wg.title,          j["label"]  | "", 32);
        strlcpy(wg.service_domain, j["service_domain"] | "", 32);
        wg.min_val = j["min"] | 0;
        wg.max_val = j["max"] | 100;
        wg.root    = nullptr;

        const char *type = j["type"] | "label";
        if      (strcmp(type, "button") == 0) { wg.type = WT_BUTTON; build_button(wg, j); }
        else if (strcmp(type, "gauge")  == 0) { wg.type = WT_GAUGE;  build_gauge(wg, j);  }
        else if (strcmp(type, "switch") == 0) { wg.type = WT_SWITCH; build_switch(wg, j); }
        else if (strcmp(type, "bar")    == 0) { wg.type = WT_BAR;    build_bar(wg, j);    }
        else                                  { wg.type = WT_LABEL;  build_label(wg, j);  }

        _count++;
    }
    Serial.printf("[UI] built %d widgets\n", _count);
}

void widgets_update(const char *entity_id, const char *state, JsonObjectConst attrs) {
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
