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

static const size_t CAL_BYTES = 5 * sizeof(uint16_t);

static void touch_key(int rotation, char *key, size_t n) {
    snprintf(key, n, "touch_cal%d", rotation & 3);
}

bool storage_load_touch(int rotation, uint16_t cal[5]) {
    char key[16];
    touch_key(rotation, key, sizeof(key));
    Preferences p;
    p.begin(NS, false);
    // One-time migration: the single calibration of the first firmware was made in landscape.
    if (p.getBytesLength(key) != CAL_BYTES && (rotation & 3) == 0 && p.getBytesLength("touch_cal") == CAL_BYTES) {
        uint16_t old[5];
        if (p.getBytes("touch_cal", old, CAL_BYTES) == CAL_BYTES) {
            p.putBytes(key, old, CAL_BYTES);
            p.remove("touch_cal");
        }
    }
    bool ok = p.getBytesLength(key) == CAL_BYTES && p.getBytes(key, cal, CAL_BYTES) == CAL_BYTES;
    p.end();
    return ok;
}

void storage_save_touch(int rotation, const uint16_t cal[5]) {
    char key[16];
    touch_key(rotation, key, sizeof(key));
    Preferences p;
    p.begin(NS, false);
    p.putBytes(key, cal, CAL_BYTES);
    p.end();
}

int storage_load_rotation() {
    Preferences p;
    p.begin(NS, true);
    int r = p.getInt("rotation", 0);
    p.end();
    return (r < 0 || r > 3) ? 0 : r;
}

void storage_save_rotation(int rotation) {
    Preferences p;
    p.begin(NS, false);
    if (p.getInt("rotation", -1) != rotation) p.putInt("rotation", rotation);
    p.end();
}
