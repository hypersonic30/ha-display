#include "ui.h"
#include "fonts.h"
#include "widgets.h"

static const lv_color_t C_BG     = LV_COLOR_MAKE(0x0f, 0x0f, 0x1a);
static const lv_color_t C_CARD   = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
static const lv_color_t C_ACCENT = LV_COLOR_MAKE(0x4f, 0xc3, 0xf7);
static const lv_color_t C_MUTED  = LV_COLOR_MAKE(0x88, 0x88, 0x88);
static const lv_color_t C_WHITE  = LV_COLOR_MAKE(0xe0, 0xe0, 0xe0);
static const lv_color_t C_ERR    = LV_COLOR_MAKE(0xef, 0x53, 0x50);

static const char *HOLD_HINT = "Touch 3 s halten: Setup";

static void clear_screen() {
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

void ui_show_provisioning(const char *ap_ssid, const char *ap_pass, const char *url) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();
    const int W = scr_w(), H = scr_h();

    lv_obj_t *title = make_label(scr, "ESP32 Display", &ui_font_24, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

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
