#include "ota.h"
#include "ha_client.h"
#include "ui.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <lvgl.h>

static const char    *NS            = "display";
static const uint32_t MIN_IMAGE     = 200 * 1024;
static const uint32_t GOOD_AFTER_MS = 10000;   // a new image must run this long with a network ...
static const uint8_t  MAX_BOOTS     = 3;       // ... or it is dropped after this many boots

struct Request {
    char     version[24];
    char     path[80];
    uint32_t size;
    char     md5[33];
};

static Request req;
static bool    pending = false;
static bool    running = false;
static char    last_error[80] = "";

// ── Software rollback ─────────────────────────────────────────────────────────
//
// The Arduino-ESP32 core ships a precompiled bootloader without CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, so
// nothing reverts an image that boots but crashes. We do it ourselves: after an update the NVS flag
// ota_pending is set; the new image clears it once it has been up for a while with a network. If it keeps
// rebooting before that, the other OTA slot (still holding the previous image) is booted again.

static bool boot_pending = false;
static bool marked_good  = false;

void ota_boot_check() {
    Preferences p;
    p.begin(NS, false);
    boot_pending = p.getUChar("ota_pending", 0) != 0;
    if (boot_pending) {
        uint8_t boots = p.getUChar("ota_boots", 0) + 1;
        Serial.printf("[OTA] first boots of a new image: %u\n", boots);
        if (boots > MAX_BOOTS) {
            p.remove("ota_pending");
            p.remove("ota_boots");
            p.end();
            const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
            if (other && esp_ota_set_boot_partition(other) == ESP_OK) {
                Serial.println("[OTA] new image never became good, going back to the previous one");
                delay(100);
                ESP.restart();
            }
            boot_pending = false;
            return;
        }
        p.putUChar("ota_boots", boots);
    }
    p.end();
}

void ota_loop_tick(bool network_ok) {
    if (marked_good) return;
    if (!boot_pending) { marked_good = true; return; }
    if (network_ok && millis() >= GOOD_AFTER_MS) {
        Preferences p;
        p.begin(NS, false);
        p.remove("ota_pending");
        p.remove("ota_boots");
        p.end();
        marked_good = true;
        Serial.println("[OTA] new image confirmed");
    }
}

// ── Request handling ──────────────────────────────────────────────────────────

static void reject(const char *why) {
    Serial.printf("[OTA] rejected: %s\n", why);
    ha_client_ota_status("error", 0, why);
}

static bool is_md5(const char *s) {
    if (strlen(s) != 32) return false;
    for (int i = 0; i < 32; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

void ota_request(const char *version, const char *path, uint32_t size, const char *md5) {
    if (pending || running)                           return reject("busy");
    if (strcmp(path, "/api/display/firmware/" FW_VARIANT) != 0) return reject("wrong firmware path");
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)                                      return reject("no update partition");
    if (size < MIN_IMAGE || size > target->size)      return reject("implausible size");
    if (!is_md5(md5))                                 return reject("bad md5");

    strlcpy(req.version, version, sizeof(req.version));
    strlcpy(req.path, path, sizeof(req.path));
    strlcpy(req.md5, md5, sizeof(req.md5));
    req.size = size;
    pending  = true;
    Serial.printf("[OTA] update to %s queued (%u bytes)\n", req.version, (unsigned)size);
}

bool ota_pending() { return pending; }
bool ota_running() { return running; }
const char *ota_last_error() { return last_error; }

// ── Download ──────────────────────────────────────────────────────────────────

// Keep the WebSocket alive (pings, status frames) and the progress bar moving while we block in the download.
static void service() {
    ha_client_tick();
    lv_task_handler();
}

static bool fail(const char *why, bool began) {
    if (began) Update.abort();
    strlcpy(last_error, why, sizeof(last_error));
    Serial.printf("[OTA] failed: %s\n", last_error);
    ha_client_ota_status("error", 0, last_error);
    running = pending = false;
    return false;
}

bool ota_run(const Config &cfg) {
    pending = false;
    running = true;
    last_error[0] = 0;

    ui_show_update(req.version);
    service();
    ha_client_ota_status("downloading", 0);

    HaUrl u;
    if (!ha_url_parse(cfg.ha_url, u)) return fail("bad Home Assistant URL", false);
    char url[256];
    snprintf(url, sizeof(url), "%s://%s:%u%s%s", u.tls ? "https" : "http", u.host,
             (unsigned)u.port, u.prefix, req.path);

    WiFiClient       plain;
    WiFiClientSecure secure;
    secure.setInsecure();   // same trust model as the WebSocket: LAN device without a CA store

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    if (!(u.tls ? http.begin(secure, url) : http.begin(plain, url))) return fail("cannot open connection", false);
    http.addHeader("X-Device-Key", cfg.device_key);

    int code = http.GET();
    if (code != 200) {
        char msg[40];
        snprintf(msg, sizeof(msg), code > 0 ? "download refused (HTTP %d)" : "download failed (%d)", code);
        http.end();
        return fail(msg, false);
    }
    int len = http.getSize();
    if (len < 0 || (uint32_t)len != req.size) {
        http.end();
        return fail("size differs from announcement", false);
    }

    // Update.begin() erases the target slot, which takes a few seconds without servicing the socket.
    if (!Update.begin(req.size, U_FLASH)) { http.end(); return fail(Update.errorString(), false); }
    if (!Update.setMD5(req.md5))          { http.end(); return fail("md5 rejected", true); }
    service();

    WiFiClient *stream = http.getStreamPtr();
    uint8_t     chunk[1536];
    uint32_t    written   = 0;
    uint32_t    last_data = millis();
    int         last_pct  = 0;

    while (written < req.size) {
        service();
        if (WiFi.status() != WL_CONNECTED) { http.end(); return fail("WiFi lost", true); }

        size_t avail = stream->available();
        if (avail == 0) {
            if (!http.connected())               { http.end(); return fail("connection lost", true); }
            if (millis() - last_data > 20000)    { http.end(); return fail("download timeout", true); }
            delay(2);
            continue;
        }
        size_t want = min(avail, min(sizeof(chunk), (size_t)(req.size - written)));
        int    n    = stream->readBytes(chunk, want);   // may be shorter than asked: only count what arrived
        if (n <= 0) continue;
        if (Update.write(chunk, n) != (size_t)n) { http.end(); return fail(Update.errorString(), true); }
        written  += n;
        last_data = millis();

        int pct = (int)((uint64_t)written * 100 / req.size);
        if (pct >= last_pct + 5 || (pct == 100 && last_pct != 100)) {
            last_pct = pct;
            ui_update_progress(pct);
            ha_client_ota_status("downloading", pct);
        }
    }
    http.end();

    // end(true): checks the md5 announced by the integration, then switches the boot partition.
    if (!Update.end(true)) return fail(Update.errorString(), false);

    Preferences p;
    p.begin(NS, false);
    p.putUChar("ota_pending", 1);
    p.putUChar("ota_boots", 0);
    p.end();

    ui_update_progress(100);
    ha_client_ota_status("done");
    for (int i = 0; i < 10; i++) { service(); delay(50); }   // let the frame leave before the restart
    Serial.println("[OTA] done, restarting");
    ESP.restart();
    return true;
}
