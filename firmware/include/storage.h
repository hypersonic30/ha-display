#pragma once
#include <Arduino.h>

struct Config {
    char wifi_ssid[64];
    char wifi_pass[64];
    char ha_url[128];
    char device_key[64];   // per-display code issued by the integration
};

bool  storage_load(Config &cfg);
void  storage_save(const Config &cfg);

// XPT2046 calibration data (5 x uint16_t, see TFT_eSPI::calibrateTouch). It depends on the screen
// rotation (0..3), so every rotation has its own set. The pre-rotation "touch_cal" becomes rotation 0.
bool  storage_load_touch(int rotation, uint16_t cal[5]);
void  storage_save_touch(int rotation, const uint16_t cal[5]);

// Last applied screen rotation (0..3, 0 = landscape), used right at boot.
int   storage_load_rotation();
void  storage_save_rotation(int rotation);
