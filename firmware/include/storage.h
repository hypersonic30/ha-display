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
    bool tls_verify;       // https: check the certificate chain and host name (default on)
};

bool  storage_load(Config &cfg);
void  storage_save(const Config &cfg);

// XPT2046 calibration: the 4 raw corner readings (8 x uint16_t, LovyanGFX format). The same data serves every
// screen rotation. (Calibrations stored by firmware before 0.7.0 used another format and are ignored.)
bool  storage_load_touch(uint16_t cal[8]);
void  storage_save_touch(const uint16_t cal[8]);

// Last applied screen rotation (0..3, 0 = landscape), used right at boot.
int   storage_load_rotation();
void  storage_save_rotation(int rotation);

// Backlight level chosen with the slider on the setup screen (10..100 %); 0 = never set.
int   storage_load_brightness();
void  storage_save_brightness(int percent);
