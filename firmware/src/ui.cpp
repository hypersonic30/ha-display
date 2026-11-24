#include "ui.h"
#include "fonts.h"
#include "widgets.h"

static const lv_color_t C_BG     = LV_COLOR_MAKE(0x0f, 0x0f, 0x1a);
static const lv_color_t C_CARD   = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
static const lv_color_t C_ACCENT = LV_COLOR_MAKE(0x4f, 0xc3, 0xf7);
static const lv_color_t C_MUTED  = LV_COLOR_MAKE(0x88, 0x88, 0x88);
static const lv_color_t C_WHITE  = LV_COLOR_MAKE(0xe0, 0xe0, 0xe0);
static const lv_color_t C_ERR    = LV_COLOR_MAKE(0xef, 0x53, 0x50);

static const char *HOLD_HINT = "Leere Stelle 3 s halten: WLAN & Setup";

static void (*cancel_cb)() = nullptr;
static void (*calibrate_cb)() = nullptr;

static lv_obj_t *update_bar = nullptr;
static lv_obj_t *update_pct = nullptr;

static void clear_screen() {
    update_bar = update_pct = nullptr;   // the update screen's objects are about to be deleted
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    widgets_reset();  // the layout objects are gone now
    lv_obj_set_style_bg_color(scr, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    return l;
}

// All screens adapt to the current rotation (480x320 or 320x480).
static int scr_w() { return lv_disp_get_hor_res(NULL); }
static int scr_h() { return lv_disp_get_ver_res(NULL); }

// A label that wraps at the given width instead of running off the screen.
static lv_obj_t *wrap_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int width) {
    lv_obj_t *l = make_label(parent, text, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return l;
}

static void add_hold_hint(lv_obj_t *scr) {
    lv_obj_t *h = wrap_label(scr, HOLD_HINT, &ui_font_12, C_MUTED, scr_w() - 20);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ui_init() {
    clear_screen();
}

// ── Provisioning screen ───────────────────────────────────────────────────────

static void cb_cancel(lv_event_t *) {
    if (cancel_cb) cancel_cb();
}

static void cb_calibrate(lv_event_t *) {
    if (calibrate_cb) calibrate_cb();
}

static void (*brightness_cb)(int, bool) = nullptr;

static void cb_brightness(lv_event_t *e) {
    if (!brightness_cb) return;
    lv_obj_t *s = lv_event_get_target(e);
    brightness_cb(lv_slider_get_value(s), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

// Label + slider in one row of `width` px.
static void make_brightness_row(lv_obj_t *parent, int width, int value) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, width, 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = make_label(row, "Hell.", &ui_font_12, C_MUTED);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *s = lv_slider_create(row);
    lv_slider_set_range(s, 10, 100);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_width(s, width - 48);
    lv_obj_set_ext_click_area(s, 12);
    lv_obj_align(s, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_color(s, C_MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, C_WHITE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(s, cb_brightness, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s, cb_brightness, LV_EVENT_RELEASED, nullptr);
}

void ui_show_provisioning(const char *ap_ssid, const char *ap_pass, const char *url,
                          void (*on_cancel)(), const char *note, void (*on_calibrate)(),
                          int brightness, void (*on_brightness)(int, bool)) {
    clear_screen();
    cancel_cb = on_cancel;
    calibrate_cb = on_calibrate;
    brightness_cb = on_brightness;
    lv_obj_t *scr = lv_scr_act();
    const int W = scr_w(), H = scr_h();

    lv_obj_t *title = make_label(scr, "WLAN & Setup", &ui_font_24, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    // Wide screens: the slider sits in the title bar; narrow ones get it at the end of the card.
    const bool slider_in_header = on_brightness && W >= 440;
    if (slider_in_header) {
        make_brightness_row(scr, on_cancel ? 150 : 220, brightness);
        lv_obj_t *row = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 1);
        lv_obj_align(row, LV_ALIGN_TOP_MID, on_cancel ? 40 : 60, 8);
    }

    if (on_cancel) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 96, 34);
        lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 6);
        lv_obj_set_style_bg_color(btn, C_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, C_MUTED, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, cb_cancel, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *bl = make_label(btn, "Abbrechen", &ui_font_14, C_WHITE);
        lv_obj_center(bl);
    }

    const int card_w = W - 20 < 440 ? W - 20 : 440;
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, card_w, H - 56);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    const int inner = card_w - 28;
    auto left = [&](const char *text, const lv_font_t *font, lv_color_t color) {
        lv_obj_t *l = make_label(card, text, font, color);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, inner);
        return l;
    };

    left("1. Mit dem WLAN verbinden", &ui_font_14, C_MUTED);

    char line[96];
    snprintf(line, sizeof(line), "Name:  %s", ap_ssid);
    left(line, &ui_font_16, C_WHITE);

    snprintf(line, sizeof(line), "Passwort:  %s", ap_pass);
    left(line, &ui_font_20, C_ACCENT);

    lv_obj_t *step2 = left("2. Browser öffnen", &ui_font_14, C_MUTED);
    lv_obj_set_style_pad_top(step2, 12, LV_PART_MAIN);

    left(url, &ui_font_20, C_ACCENT);

    if (note && note[0]) left(note, &ui_font_12, C_MUTED);

    if (on_calibrate) {
        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, 170, 30);
        lv_obj_set_style_bg_color(btn, C_BG, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, C_MUTED, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, cb_calibrate, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *bl = make_label(btn, "Touch kalibrieren", &ui_font_14, C_WHITE);
        lv_obj_center(bl);
    }

    if (on_brightness && !slider_in_header) make_brightness_row(card, inner, brightness);
}

// ── Connecting screen ─────────────────────────────────────────────────────────

void ui_show_connecting(const char *msg) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl = wrap_label(scr, msg, &ui_font_16, C_MUTED, scr_w() - 40);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 40);
    add_hold_hint(scr);
}

// ── Error screen ──────────────────────────────────────────────────────────────

void ui_show_error(const char *msg) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *icon = make_label(scr, "!", &ui_font_24, C_ERR);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *lbl = wrap_label(scr, msg, &ui_font_16, C_ERR, scr_w() - 40);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    add_hold_hint(scr);
}

// ── Running screen (until the layout arrives) ─────────────────────────────────

void ui_show_running(const char *ha_url) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *lbl = make_label(scr, "Verbinde ...", &ui_font_24, C_ACCENT);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *sub = wrap_label(scr, ha_url, &ui_font_12, C_MUTED, scr_w() - 20);
    lv_obj_align_to(sub, lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_t *placeholder = wrap_label(scr, "Warte auf Layout ...", &ui_font_16, C_MUTED, scr_w() - 40);
    lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, 20);
    add_hold_hint(scr);
}

// ── Firmware update screen ────────────────────────────────────────────────────

void ui_show_update(const char *version) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();
    const int W = scr_w();

    lv_obj_t *title = make_label(scr, "Firmware-Update", &ui_font_24, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);

    char line[48];
    snprintf(line, sizeof(line), "Version %s", version && version[0] ? version : "?");
    lv_obj_t *sub = wrap_label(scr, line, &ui_font_14, C_MUTED, W - 40);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -30);

    update_bar = lv_bar_create(scr);
    lv_obj_set_size(update_bar, W - 60 < 360 ? W - 60 : 360, 16);
    lv_obj_align(update_bar, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(update_bar, 0, 100);
    lv_bar_set_value(update_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(update_bar, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(update_bar, C_ACCENT, LV_PART_INDICATOR);

    update_pct = make_label(scr, "0 %", &ui_font_20, C_WHITE);
    lv_obj_align(update_pct, LV_ALIGN_CENTER, 0, 44);

    lv_obj_t *hint = wrap_label(scr, "Bitte nicht ausschalten", &ui_font_14, C_MUTED, W - 40);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
}

void ui_update_progress(int percent) {
    if (!update_bar || !update_pct) return;
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    lv_bar_set_value(update_bar, percent, LV_ANIM_OFF);
    char text[12];
    snprintf(text, sizeof(text), "%d %%", percent);
    lv_label_set_text(update_pct, text);
}
