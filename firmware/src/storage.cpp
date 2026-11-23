#include "storage.h"
#include <Preferences.h>

static const char *NS = "display";

bool storage_load(Config &cfg) {
    Preferences p;
    p.begin(NS, true);
    bool valid = p.getBool("valid", false);
    if (valid) {
        p.getString("wifi_ssid",    cfg.wifi_ssid,    sizeof(cfg.wifi_ssid));
        p.getString("wifi_pass",    cfg.wifi_pass,    sizeof(cfg.wifi_pass));
        p.getString("ha_url",       cfg.ha_url,       sizeof(cfg.ha_url));
        p.getString("ha_token",     cfg.ha_token,     sizeof(cfg.ha_token));
        p.getString("device_name",  cfg.device_name,  sizeof(cfg.device_name));
    }
    p.end();
    return valid;
}

void storage_save(const Config &cfg) {
    Preferences p;
    p.begin(NS, false);
    p.putString("wifi_ssid",   cfg.wifi_ssid);
    p.putString("wifi_pass",   cfg.wifi_pass);
    p.putString("ha_url",      cfg.ha_url);
    p.putString("ha_token",    cfg.ha_token);
    p.putString("device_name", cfg.device_name);
    p.putBool("valid", true);
    p.end();
}

void storage_clear() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}
