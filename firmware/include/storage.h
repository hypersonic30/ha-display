#pragma once
#include <Arduino.h>

#define MAX_WIFI 3   // saved WLANs, tried in order of signal strength

struct WifiNet {
    char ssid[33];
    char pass[64];
};

struct Config {
    WifiNet wifi[MAX_WIFI];
    int  wifi_count;       // 1..MAX_WIFI, no gaps
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
