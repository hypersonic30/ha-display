#include "ui.h"
#include <TFT_eSPI.h>

static const lv_color_t C_BG     = LV_COLOR_MAKE(0x0f, 0x0f, 0x1a);
static const lv_color_t C_CARD   = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
static const lv_color_t C_ACCENT = LV_COLOR_MAKE(0x4f, 0xc3, 0xf7);
static const lv_color_t C_MUTED  = LV_COLOR_MAKE(0x88, 0x88, 0x88);
static const lv_color_t C_WHITE  = LV_COLOR_MAKE(0xe0, 0xe0, 0xe0);
static const lv_color_t C_ERR    = LV_COLOR_MAKE(0xef, 0x53, 0x50);
static const lv_color_t C_OK     = LV_COLOR_MAKE(0x66, 0xbb, 0x6a);

static void clear_screen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
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

void ui_init() {
    clear_screen();
}

// ── Provisioning screen ───────────────────────────────────────────────────────

void ui_show_provisioning(const char *ap_ssid, const char *url) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    // Title
    lv_obj_t *title = make_label(scr, "ESP32 Display", &lv_font_montserrat_24, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *sub = make_label(scr, "Einrichtung erforderlich", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    // Card
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 440, 170);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 20, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi section
    lv_obj_t *wifi_lbl = make_label(card, "WLAN Hotspot verbinden", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(wifi_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    char ssid_line[80];
    snprintf(ssid_line, sizeof(ssid_line), "SSID:  %s  (offen)", ap_ssid);
    lv_obj_t *ssid_lbl = make_label(card, ssid_line, &lv_font_montserrat_16, C_WHITE);
    lv_obj_align_to(ssid_lbl, wifi_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    // Divider
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, 400, 1);
    lv_obj_align_to(div, ssid_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
    lv_obj_set_style_bg_color(div, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(div, C_MUTED, LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 1, LV_PART_MAIN);

    // Browser section
    lv_obj_t *br_lbl = make_label(card, "Browser öffnen:", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align_to(br_lbl, div, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *url_lbl = make_label(card, url, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align_to(url_lbl, br_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

// ── Connecting screen ─────────────────────────────────────────────────────────

void ui_show_connecting(const char *msg) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *lbl = make_label(scr, msg, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 40);
}

// ── Error screen ──────────────────────────────────────────────────────────────

void ui_show_error(const char *msg) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *icon = make_label(scr, "!", &lv_font_montserrat_24, C_ERR);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl = make_label(scr, msg, &lv_font_montserrat_14, C_ERR);
    lv_obj_set_width(lbl, 400);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *sub = make_label(scr, "Startet neu...", &lv_font_montserrat_12, C_MUTED);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 70);
}

// ── Running screen (placeholder) ──────────────────────────────────────────────

void ui_show_running(const char *device_name, const char *ha_url) {
    clear_screen();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *lbl = make_label(scr, device_name, &lv_font_montserrat_24, C_ACCENT);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *sub = make_label(scr, ha_url, &lv_font_montserrat_12, C_MUTED);
    lv_obj_align_to(sub, lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_t *placeholder = make_label(scr, "Layout wird geladen...", &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, 20);
}
