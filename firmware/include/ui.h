#pragma once
#include <lvgl.h>

void ui_init();
void ui_show_provisioning(const char *ap_ssid, const char *ap_pass, const char *url);
void ui_show_connecting(const char *msg);
void ui_show_error(const char *msg);
void ui_show_running(const char *ha_url);

// Firmware update: a full-screen progress view (percent 0..100).
void ui_show_update(const char *version);
void ui_update_progress(int percent);
