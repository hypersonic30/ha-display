#include "storage.h"
#include <Preferences.h>
#include <cstring>

static const char *NS = "display";

// Slot 0 keeps the key names of the first firmware ("wifi_ssid"/"wifi_pass"), so an update keeps the
// saved network. Further slots are "wifi_ssid1"/"wifi_pass1", "wifi_ssid2"/"wifi_pass2".
static void wifi_keys(int i, char *ks, char *kp, size_t n) {
    if (i == 0) { snprintf(ks, n, "wifi_ssid"); snprintf(kp, n, "wifi_pass"); }
    else        { snprintf(ks, n, "wifi_ssid%d", i); snprintf(kp, n, "wifi_pass%d", i); }
}

bool storage_load(Config &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.tls_verify = true;
    Preferences p;
    p.begin(NS, true);
    bool valid = p.getBool("valid", false) && p.isKey("device_key");
    if (valid) {
        for (int i = 0; i < MAX_WIFI; i++) {
            char ks[16], kp[16];
            wifi_keys(i, ks, kp, sizeof(ks));
            WifiNet &w = cfg.wifi[cfg.wifi_count];
            p.getString(ks, w.ssid, sizeof(w.ssid));
            p.getString(kp, w.pass, sizeof(w.pass));
            if (w.ssid[0]) cfg.wifi_count++;
            else           memset(&w, 0, sizeof(w));
        }
        p.getString("ha_url",       cfg.ha_url,       sizeof(cfg.ha_url));
        p.getString("device_key",   cfg.device_key,   sizeof(cfg.device_key));
        cfg.tls_verify = p.getBool("tls_verify", true);
        if (cfg.wifi_count == 0) valid = false;
    }
    p.end();
    return valid;
}

void storage_save(const Config &cfg) {
    Preferences p;
    p.begin(NS, false);
    for (int i = 0; i < MAX_WIFI; i++) {
        char ks[16], kp[16];
        wifi_keys(i, ks, kp, sizeof(ks));
        if (i < cfg.wifi_count) {
            p.putString(ks, cfg.wifi[i].ssid);
            p.putString(kp, cfg.wifi[i].pass);
        } else {
            p.remove(ks);
            p.remove(kp);
        }
    }
    p.putString("ha_url",      cfg.ha_url);
    p.putString("device_key",  cfg.device_key);
    p.putBool("tls_verify", cfg.tls_verify);
    p.putBool("valid", true);
    p.end();
}

static const size_t CAL_BYTES = 8 * sizeof(uint16_t);

bool storage_load_touch(uint16_t cal[8]) {
    Preferences p;
    p.begin(NS, true);
    bool ok = p.getBytesLength("touch_lg") == CAL_BYTES && p.getBytes("touch_lg", cal, CAL_BYTES) == CAL_BYTES;
    p.end();
    return ok;
}

void storage_save_touch(const uint16_t cal[8]) {
    Preferences p;
    p.begin(NS, false);
    p.putBytes("touch_lg", cal, CAL_BYTES);
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
