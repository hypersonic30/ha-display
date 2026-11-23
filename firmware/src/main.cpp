#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "storage.h"
#include "provisioning.h"
#include "ui.h"
#include "ha_client.h"
#include "widgets.h"

// ── Display & LVGL ────────────────────────────────────────────────────────────

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf[LV_HOR_RES_MAX * 10];

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1,
                      area->x2 - area->x1 + 1,
                      area->y2 - area->y1 + 1);
    tft.pushColors((uint16_t *)&color_p->full,
                   (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    data->state = tft.getTouch(&x, &y) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
}

static void display_init() {
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 220);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = 480;
    disp_drv.ver_res  = 320;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

// ── AP SSID ───────────────────────────────────────────────────────────────────

static char ap_ssid[32];

static void build_ap_ssid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-Display-%02X%02X", mac[4], mac[5]);
}

// ── HA connection test ────────────────────────────────────────────────────────

static bool ha_reachable(const Config &cfg) {
    char url[160];
    snprintf(url, sizeof(url), "%s/api/", cfg.ha_url);
    char auth[540];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.ha_token);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", auth);
    int code = http.GET();
    http.end();
    Serial.printf("[HA] GET %s → %d\n", url, code);
    return code == 200 || code == 201;
}

// ── State machine ─────────────────────────────────────────────────────────────

enum State { PROVISIONING, CONNECTING_WIFI, TESTING_HA, RUNNING };
static State  state;
static Config cfg;
static uint32_t connect_start;
static uint8_t  wifi_retries;

static void enter_provisioning() {
    state = PROVISIONING;
    build_ap_ssid();
    provisioning_begin(ap_ssid);
    ui_show_provisioning(ap_ssid, "192.168.4.1");
    Serial.printf("[STATE] → PROVISIONING  SSID: %s\n", ap_ssid);
}

static void enter_connecting_wifi() {
    state = CONNECTING_WIFI;
    provisioning_end();
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
    connect_start = millis();
    wifi_retries  = 0;
    ui_show_connecting("Verbinde mit WLAN...");
    Serial.printf("[STATE] → CONNECTING_WIFI  SSID: %s\n", cfg.wifi_ssid);
}

static void enter_testing_ha() {
    state = TESTING_HA;
    ui_show_connecting("Prüfe Home Assistant...");
    Serial.printf("[STATE] → TESTING_HA  %s\n", cfg.ha_url);
}

// ── Entity state callback ─────────────────────────────────────────────────────

static void on_entity_state(const char *entity_id, const char *state_val,
                             JsonObjectConst attributes) {
    Serial.printf("[STATE] %s = %s\n", entity_id, state_val);
    widgets_update(entity_id, state_val, attributes);
}

// ── Layout callback ───────────────────────────────────────────────────────────

static void on_layout(JsonArrayConst widgets) {
    // Build LVGL widgets
    widgets_build(widgets);

    // Collect entity IDs and subscribe to state changes
    static const char *entity_ids[32];
    static char        entity_buf[32][64];
    size_t count = 0;

    for (JsonObjectConst w : widgets) {
        const char *eid = w["entity"] | "";
        if (eid[0] && count < 32) {
            strlcpy(entity_buf[count], eid, 64);
            entity_ids[count] = entity_buf[count];
            count++;
        }
    }
    if (count) ha_client_subscribe(entity_ids, count);
}

static void enter_running() {
    state = RUNNING;
    storage_save(cfg);
    ui_show_running(cfg.device_name, cfg.ha_url);
    ha_client_begin(cfg, on_entity_state, on_layout);
    Serial.println("[STATE] → RUNNING");
}

static void enter_error(const char *msg, bool clear_storage = false) {
    if (clear_storage) storage_clear();
    ui_show_error(msg);
    Serial.printf("[ERROR] %s\n", msg);
    delay(4000);
    ESP.restart();
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    display_init();
    ui_init();
    build_ap_ssid();

    if (storage_load(cfg)) {
        Serial.printf("[BOOT] Credentials found, connecting to %s\n", cfg.wifi_ssid);
        enter_connecting_wifi();
    } else {
        Serial.println("[BOOT] No credentials, starting provisioning");
        enter_provisioning();
    }
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    lv_task_handler();

    switch (state) {

    case PROVISIONING:
        provisioning_handle();
        if (provisioning_done()) {
            cfg = provisioning_config();
            enter_connecting_wifi();
        }
        break;

    case CONNECTING_WIFI:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
            enter_testing_ha();
        } else if (millis() - connect_start > 20000) {
            // Timeout — back to provisioning (don't clear storage, let user correct)
            WiFi.disconnect();
            enter_error("WLAN-Verbindung fehlgeschlagen.\nZurück zur Einrichtung.", true);
        }
        break;

    case TESTING_HA:
        if (ha_reachable(cfg)) {
            enter_running();
        } else {
            enter_error("Home Assistant nicht erreichbar.\nURL oder Token prüfen.", true);
        }
        break;

    case RUNNING:
        ha_client_tick();
        break;
    }

    delay(5);
}
