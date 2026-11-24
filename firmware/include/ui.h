#pragma once
#include <lvgl.h>

void ui_init();
// Setup hotspot screen. `on_cancel` != null adds an "Abbrechen" button (called from the LVGL task: only set a
// flag there); `note` is an optional muted line below the address; `on_calibrate` != null adds a "Touch
// kalibrieren" button (also only sets a flag), so the touch can be calibrated without a phone.
// `on_brightness` != null adds a backlight slider (10..100 %, starting at `brightness`); `final` = finger lifted.
void ui_show_provisioning(const char *ap_ssid, const char *ap_pass, const char *url,
                          void (*on_cancel)() = nullptr, const char *note = nullptr,
                          void (*on_calibrate)() = nullptr,
                          int brightness = 0, void (*on_brightness)(int percent, bool final) = nullptr);
void ui_show_connecting(const char *msg);
void ui_show_error(const char *msg);
void ui_show_running(const char *ha_url);

// Firmware update: a full-screen progress view (percent 0..100).
void ui_show_update(const char *version);
void ui_update_progress(int percent);
