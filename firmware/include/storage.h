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
void  storage_clear();
