#include "storage.h"
#include <Preferences.h>

static const char *NS = "display";

bool storage_load(Config &cfg) {
    Preferences p;
    p.begin(NS, true);
    bool valid = p.getBool("valid", false) && p.isKey("device_key");
    if (valid) {
        p.getString("wifi_ssid",    cfg.wifi_ssid,    sizeof(cfg.wifi_ssid));
        p.getString("wifi_pass",    cfg.wifi_pass,    sizeof(cfg.wifi_pass));
        p.getString("ha_url",       cfg.ha_url,       sizeof(cfg.ha_url));
        p.getString("device_key",   cfg.device_key,   sizeof(cfg.device_key));
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
    p.putString("device_key",  cfg.device_key);
    p.putBool("valid", true);
    p.end();
}

bool storage_load_touch(uint16_t cal[5]) {
    Preferences p;
    p.begin(NS, true);
    bool ok = p.getBytesLength("touch_cal") == 5 * sizeof(uint16_t) &&
              p.getBytes("touch_cal", cal, 5 * sizeof(uint16_t)) == 5 * sizeof(uint16_t);
    p.end();
    return ok;
}

void storage_save_touch(const uint16_t cal[5]) {
    Preferences p;
    p.begin(NS, false);
    p.putBytes("touch_cal", cal, 5 * sizeof(uint16_t));
    p.end();
}
