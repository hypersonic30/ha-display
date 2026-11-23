#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "storage.h"
#include "provisioning.h"
#include "ui.h"
#include "ha_client.h"
#include "widgets.h"

// ── Display, touch & LVGL ─────────────────────────────────────────────────────

static const int DEFAULT_BRIGHTNESS = 86;   // percent, until the layout says otherwise
static const uint32_t HOLD_MS       = 3000;

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf[LV_HOR_RES_MAX * 32];

static uint16_t touch_x = 0, touch_y = 0;
static uint32_t press_since = 0;   // 0 = not pressed

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
    if (tft.getTouch(&x, &y)) {
        touch_x = x;
        touch_y = y;
        if (!press_since) press_since = millis() ? millis() : 1;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        press_since = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = touch_x;
    data->point.y = touch_y;
}

static uint32_t touch_hold_ms() {
    return press_since ? millis() - press_since : 0;
}

static void set_brightness(int percent) {
    percent = constrain(percent, 1, 100);
    ledcWrite(TFT_BL, percent * 255 / 100);
}

static void tft_init() {
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    ledcAttach(TFT_BL, 5000, 8);
    set_brightness(DEFAULT_BRIGHTNESS);
}

// Held for HOLD_MS right after power-up: calibrate the touch again.
static bool boot_hold_requested() {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("Touch 3 s halten: Touch neu kalibrieren", 240, 150, 2);

    uint32_t t0 = millis(), held = 0;
    uint16_t x, y;
    while (millis() - t0 < HOLD_MS + 1000) {
        if (tft.getTouch(&x, &y)) {
            if (!held) held = millis();
            if (millis() - held >= HOLD_MS) return true;
        } else if (held) {
            return false;                        // released early
        } else if (millis() - t0 > 800) {
            return false;                        // nobody is touching
        }
        delay(10);
    }
    return false;
}

static void touch_init() {
    uint16_t cal[5];
    bool have = storage_load_touch(cal);
    if (have) tft.setTouch(cal);

    bool recalibrate = boot_hold_requested();
    if (!have || recalibrate) {
        tft.fillScreen(TFT_BLACK);
        tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);
        storage_save_touch(cal);
        tft.setTouch(cal);
    }
    tft.fillScreen(TFT_BLACK);
}

static void lvgl_init() {
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 32);

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
    uint64_t mac = ESP.getEfuseMac();   // valid before WiFi is started
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-Display-%02X%02X",
             (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
}

// ── State machine ─────────────────────────────────────────────────────────────

enum State { PROVISIONING, CONNECTING_WIFI, TESTING_HA, RUNNING };
static State  state;
static Config cfg;

static uint32_t connect_start;
static bool     wifi_fail_shown;
static uint32_t next_ha_try;
static bool     ha_fail_shown;
static bool     ha_missing;
static HaStatus last_ha_status;

static bool    status_screen = false;  // a screen without widgets: hold touch -> setup
static int32_t built_rev     = -1;     // layout revision currently on screen

static void screen_connecting(const char *msg) {
    status_screen = true;
    built_rev = -1;
    ui_show_connecting(msg);
}

static void screen_error(const char *msg) {
    status_screen = true;
    built_rev = -1;
    ui_show_error(msg);
    Serial.printf("[ERROR] %s\n", msg);
}

static void enter_provisioning() {
    ha_client_stop();
    WiFi.disconnect();
    state = PROVISIONING;
    status_screen = false;
    built_rev = -1;
    provisioning_begin(ap_ssid);
    ui_show_provisioning(ap_ssid, provisioning_ap_pass(), "192.168.4.1");
    Serial.printf("[STATE] -> PROVISIONING  SSID: %s\n", ap_ssid);
}

static void wifi_begin() {
    WiFi.disconnect();
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
    connect_start = millis();
}

static void enter_connecting_wifi() {
    state = CONNECTING_WIFI;
    wifi_fail_shown = false;
    wifi_begin();
    screen_connecting("Verbinde mit WLAN...");
    Serial.printf("[STATE] -> CONNECTING_WIFI  SSID: %s\n", cfg.wifi_ssid);
}

static void enter_testing_ha() {
    state = TESTING_HA;
    ha_fail_shown = false;
    ha_missing = false;
    next_ha_try = 0;
    screen_connecting("Prüfe Home Assistant...");
    Serial.printf("[STATE] -> TESTING_HA  %s\n", cfg.ha_url);
}

// ── HA callbacks ──────────────────────────────────────────────────────────────

static void on_entity_state(const char *entity_id, const char *state_val) {
    widgets_update(entity_id, state_val);
}

static void on_layout(JsonObjectConst ev) {
    int brightness = ev["settings"]["brightness"] | 0;
    if (brightness > 0) set_brightness(brightness);

    JsonArrayConst widgets = ev["widgets"].as<JsonArrayConst>();
    int32_t rev = ev["rev"] | 0;
    if (rev != built_rev) {
        int n = widgets_build(widgets, ev["name"] | "Display");
        built_rev = rev;
        status_screen = (n == 0);
    }
}

static void enter_running() {
    state = RUNNING;
    status_screen = true;
    built_rev = -1;
    last_ha_status = HA_CONNECTING;
    ui_show_running(cfg.ha_url);
    ha_client_begin(cfg, on_entity_state, on_layout);
    Serial.println("[STATE] -> RUNNING");
}

static void check_ha_status() {
    HaStatus st = ha_client_status();
    if (st == last_ha_status) return;
    last_ha_status = st;

    char msg[160];
    switch (st) {
    case HA_AUTH_FAILED:
        screen_error("Gerätecode ungültig / abgelehnt.\nIn Home Assistant unter 'Einrichtung' prüfen.");
        break;
    case HA_PROTOCOL_ERROR:
        snprintf(msg, sizeof(msg), "Home Assistant lehnt die Verbindung ab:\n%s", ha_client_error());
        screen_error(msg);
        break;
    default:
        break;   // CONNECTING keeps whatever is on screen, OK is shown by the layout itself
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    tft_init();
    touch_init();
    lvgl_init();
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

    if (state != PROVISIONING && status_screen && touch_hold_ms() >= HOLD_MS) {
        press_since = 0;
        enter_provisioning();
        delay(5);
        return;
    }

    switch (state) {

    case PROVISIONING:
        provisioning_handle();
        if (provisioning_done()) {
            cfg = provisioning_config();
            storage_save(cfg);
            provisioning_end();
            enter_connecting_wifi();
        }
        break;

    case CONNECTING_WIFI:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
            enter_testing_ha();
        } else if (millis() - connect_start > 20000) {
            if (!wifi_fail_shown) {
                char msg[128];
                snprintf(msg, sizeof(msg), "WLAN '%s' nicht erreichbar.\nNeuer Versuch läuft...",
                         cfg.wifi_ssid);
                screen_error(msg);
                wifi_fail_shown = true;
            }
            wifi_begin();
        }
        break;

    case TESTING_HA:
        if (WiFi.status() != WL_CONNECTED) { enter_connecting_wifi(); break; }
        if (millis() < next_ha_try) break;
        {
            int code = ha_check(cfg);
            if (code == 200 || code == 201) {
                enter_running();
            } else {
                // 404 = reachable but no integration there; anything else = not reachable
                bool missing = (code == 404);
                if (!ha_fail_shown || missing != ha_missing) {
                    screen_error(missing
                        ? "Integration 'ESP32 Display' nicht gefunden.\nURL prüfen. Neuer Versuch läuft..."
                        : "Home Assistant nicht erreichbar.\nURL prüfen. Neuer Versuch läuft...");
                    ha_fail_shown = true;
                    ha_missing = missing;
                }
                next_ha_try = millis() + 10000;
            }
        }
        break;

    case RUNNING:
        ha_client_tick();
        check_ha_status();
        break;
    }

    delay(5);
}
