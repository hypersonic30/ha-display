#pragma once
#include <Arduino.h>

struct Config {
    char wifi_ssid[64];
    char wifi_pass[64];
    char ha_url[128];
    char ha_token[512];
    char device_name[64];
};

bool  storage_load(Config &cfg);
void  storage_save(const Config &cfg);

// XPT2046 calibration data (5 x uint16_t, see TFT_eSPI::calibrateTouch)
bool  storage_load_touch(uint16_t cal[5]);
void  storage_save_touch(const uint16_t cal[5]);
