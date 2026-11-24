#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "storage.h"
#include "provisioning.h"
#include "ui.h"
#include "ha_client.h"
#include "widgets.h"
#include "ota.h"
#include "tls.h"
#include "display.h"
#include "hw.h"
#include "hw_json.h"
#include "buzzer.h"

// ── Display, touch & LVGL ─────────────────────────────────────────────────────

static const int DEFAULT_BRIGHTNESS = 86;   // percent, until the layout says otherwise
static const uint32_t HOLD_MS       = 3000;

static HwConfig hw_cfg;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf[LV_HOR_RES_MAX * 32];   // fits 480x32 as well as 320x48 lines
static lv_disp_drv_t      disp_drv;
static lv_disp_t         *disp = nullptr;

static uint16_t touch_x = 0, touch_y = 0;
static uint32_t press_since = 0;   // 0 = not pressed
static int      press_x = 0, press_y = 0;   // where the current press started
static bool     press_moved = false;        // the finger travelled: a drag, not a hold

// Screen rotation 0..3 (0 = landscape 480x320, 1 = portrait 320x480, ...). The layout decides it.
static int  cur_rotation     = 0;
static bool calibrate_pending = false;   // set inside a callback, done from loop()
static bool touch_uncalibrated = false;  // running on default data, nothing stored yet
static bool calibration_offered = false; // the one automatic offer per boot has been made
static bool click_enabled = false;       // layout setting `click`: a short beep on every press of an operable widget
static bool portal_calibrate_req = false;// the "Touch kalibrieren" button on the hotspot screen (flag only)

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    display_flush(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)&color_p->full);
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    if (display_touch(x, y)) {
        touch_x = x;
        touch_y = y;
        if (!press_since) {
            press_since = millis() ? millis() : 1;
            press_x = x; press_y = y; press_moved = false;
            if (click_enabled && widgets_input_at(x, y)) buzzer_click();
        } else if (abs((int)x - press_x) > 20 || abs((int)y - press_y) > 20) {
            press_moved = true;
        }
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

static int cur_brightness = DEFAULT_BRIGHTNESS;
static void set_brightness(int percent) { cur_brightness = percent; display_set_brightness(percent); }

// Slider on the setup screen: live while dragging, stored when the finger is lifted.
static void on_portal_brightness(int percent, bool final) {
    set_brightness(percent);
    if (final) storage_save_brightness(percent);
}

static void tft_init() {
    cur_rotation = storage_load_rotation();
    display_begin(hw_cfg);
    display_set_rotation(cur_rotation);
    display_fill_black();
    int saved = storage_load_brightness();
    set_brightness(saved ? saved : DEFAULT_BRIGHTNESS);
    display_print_ids();
}

// Held for HOLD_MS right after power-up: calibrate the touch again.
static bool boot_hold_requested() {
    display_text_centered("Touch 3 s halten: Touch neu kalibrieren", display_width() / 2, display_height() / 2 - 10, 1, true);

    uint32_t t0 = millis(), held = 0;
    uint16_t x, y;
    while (millis() - t0 < HOLD_MS + 1000) {
        hw_guard_poll();
        if (display_touch(x, y)) {
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

// Blocking for at most about a minute: corner markers, then store the result (it is the same for every
// rotation). Without any touch it gives up, keeps the default data and does not store anything (the next boot
// offers it again).
static bool run_calibration() {
    uint16_t cal[8];
    bool stored = false;
    Serial.println("[TOUCH] calibration started");
    display_fill_black();
    display_text_centered("Touch kalibrieren", display_width() / 2, display_height() / 2 - 24, 2, false);
    display_text_centered("Markierungen an den Ecken antippen", display_width() / 2, display_height() / 2 + 16, 1, false);
    delay(1500);
    if (display_calibrate(cal, 60000)) {
        storage_save_touch(cal);
        touch_uncalibrated = false;
        stored = true;
        Serial.println("[TOUCH] calibration stored");
    } else {
        Serial.println("[TOUCH] no touch within 60 s, keeping the current data");
    }
    display_fill_black();
    press_since = 0;
    return stored;
}

// Never blocks without a user action: no stored data means default data and the normal start; the
// calibration is offered later (see loop()). Holding the screen at power-up still forces it.
static void touch_init() {
    uint16_t cal[8];
    if (storage_load_touch(cal)) {
        display_apply_calibration(cal);
    } else {
        touch_uncalibrated = true;
        Serial.println("[TOUCH] no stored calibration, using defaults");
    }

    if (hw_cfg.touch_on && boot_hold_requested()) run_calibration();
    display_fill_black();
}

static void lvgl_init() {
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 32);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = display_width();
    disp_drv.ver_res  = display_height();
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp = lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

// Switch the whole display to another rotation at runtime. The caller builds the screen afterwards.
static void apply_rotation(int r) {
    cur_rotation = r;
    storage_save_rotation(r);
    display_set_rotation(r);
    disp_drv.hor_res = display_width();
    disp_drv.ver_res = display_height();
    lv_disp_drv_update(disp, &disp_drv);
    display_fill_black();
    press_since = 0;
    Serial.printf("[UI] rotation %d (%dx%d)\n", r, display_width(), display_height());
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

static const uint32_t WIFI_TRY_MS      = 15000;    // per saved network
static const uint32_t AUTO_PORTAL_MS   = 90000;    // no saved WLAN reachable this long -> open the setup hotspot
static const uint32_t SAVE_GRACE_MS    = 300000;   // after a fresh save: time to switch a phone hotspot back on
static uint32_t auto_portal_ms         = AUTO_PORTAL_MS;
static const uint32_t PORTAL_TIMEOUT_MS = 300000;  // a portal opened by hand closes itself
static const uint32_t WIFI_LOST_MS     = 8000;     // RUNNING without WLAN this long -> reconnect logic

static uint32_t connect_start;
static bool     wifi_fail_shown;
static uint32_t next_ha_try;
static bool     ha_fail_shown;
static bool     ha_missing;
static HaStatus last_ha_status;

// Saved networks in the order they are tried, and the one being tried.
static int      net_order[MAX_WIFI];
static int      net_pos = 0;
static uint32_t offline_since = 0;   // 0 = online; else when the WLAN was lost / boot started
static uint32_t wifi_lost_at  = 0;   // RUNNING: first moment without WLAN

// Setup hotspot: either the only thing running (PROVISIONING) or on top of the connection attempts
// (portal_overlay, state stays CONNECTING_WIFI).
static bool     portal_overlay   = false;
static bool     portal_manual    = false;   // opened by a long press (closes after PORTAL_TIMEOUT_MS)
static bool     portal_cancelable = false;
static bool     portal_cancel    = false;   // set by the Abbrechen button
static uint32_t portal_opened    = 0;

static bool    status_screen = false;  // a screen without widgets
static int32_t built_rev     = -1;     // layout revision currently on screen
static int     built_page    = -1;     // page index currently on screen

static void screen_connecting(const char *msg) {
    status_screen = true;
    built_rev = -1; built_page = -1;
    ui_show_connecting(msg);
}

static void screen_error(const char *msg) {
    status_screen = true;
    built_rev = -1; built_page = -1;
    ui_show_error(msg);
    Serial.printf("[ERROR] %s\n", msg);
}

static void on_portal_cancel() { portal_cancel = true; }   // LVGL callback: act from loop()
static void on_portal_calibrate() { portal_calibrate_req = true; }
static char portal_note[96] = "";

static void show_portal(const char *note) {
    status_screen = false;
    built_rev = -1; built_page = -1;
    portal_cancel = false;
    portal_opened = millis();
    strlcpy(portal_note, note ? note : "", sizeof(portal_note));
    ui_show_provisioning(ap_ssid, provisioning_ap_pass(), "192.168.4.1",
                         portal_cancelable ? on_portal_cancel : nullptr, note, hw_cfg.touch_on ? on_portal_calibrate : nullptr,
                         cur_brightness, hw_cfg.bl >= 0 ? on_portal_brightness : nullptr);
}

// First boot (no saved WLAN) or opened by hand from a running page: the hotspot is all that runs.
static void enter_provisioning(bool by_hand) {
    ha_client_stop();
    if (!by_hand) WiFi.disconnect();   // by hand the station stays connected
    state = PROVISIONING;
    portal_overlay = false;
    portal_manual = by_hand;
    portal_cancelable = by_hand;
    provisioning_begin(ap_ssid, cfg.wifi_count ? &cfg : nullptr);
    show_portal(by_hand ? "Ohne Eingabe läuft das Display nach 5 Minuten normal weiter." : nullptr);
    Serial.printf("[STATE] -> PROVISIONING  SSID: %s\n", ap_ssid);
}

// While the display is offline: open the hotspot next to the connection attempts, which keep running.
// A saved network that comes back closes it again.
static void open_portal_overlay(bool by_hand) {
    portal_overlay = true;
    portal_manual = by_hand;
    portal_cancelable = true;
    provisioning_begin(ap_ssid, &cfg);
    show_portal("Die gespeicherten WLANs werden weiter versucht.");
    Serial.printf("[PORTAL] overlay opened (%s)\n", by_hand ? "by hand" : "no WLAN for 90 s");
}

static void close_portal() {
    provisioning_end();
    portal_overlay = false;
    portal_manual = false;
    portal_cancelable = false;
    portal_cancel = false;
}

// Order the saved networks: visible ones by signal strength first (only worth a scan with several; skipped
// while the hotspot is up because a scan would disturb it), the rest in saved order.
static void wifi_plan() {
    int n = cfg.wifi_count;
    int rssi[MAX_WIFI];
    for (int i = 0; i < n; i++) { net_order[i] = i; rssi[i] = -1000; }
    if (n > 1 && !portal_overlay) {
        int found = WiFi.scanNetworks();
        for (int k = 0; k < found; k++) {
            String ss = WiFi.SSID(k);
            for (int i = 0; i < n; i++)
                if (ss == cfg.wifi[i].ssid && WiFi.RSSI(k) > rssi[i]) rssi[i] = WiFi.RSSI(k);
        }
        WiFi.scanDelete();
        for (int i = 1; i < n; i++) {   // stable insertion sort, strongest first
            int v = net_order[i], j = i - 1;
            while (j >= 0 && rssi[net_order[j]] < rssi[v]) { net_order[j + 1] = net_order[j]; j--; }
            net_order[j + 1] = v;
        }
    }
    net_pos = 0;
}

static void wifi_begin() {
    const WifiNet &w = cfg.wifi[net_order[net_pos]];
    WiFi.disconnect();
    WiFi.begin(w.ssid, w.pass);
    connect_start = millis();
    Serial.printf("[WIFI] trying '%s' (%d/%d)\n", w.ssid, net_pos + 1, cfg.wifi_count);
}

static void enter_connecting_wifi() {
    state = CONNECTING_WIFI;
    wifi_fail_shown = false;
    if (!offline_since) offline_since = millis() ? millis() : 1;
    wifi_plan();
    wifi_begin();
    screen_connecting("Verbinde mit WLAN...");
    Serial.printf("[STATE] -> CONNECTING_WIFI  %d saved network(s)\n", cfg.wifi_count);
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

static void on_frame(const char *type, JsonObjectConst ev) {
    if (ota_running()) return;
    if (strcmp(type, "options") == 0)   widgets_options(ev);
    else if (strcmp(type, "time") == 0) widgets_time(ev);
    else if (strcmp(type, "beep") == 0) buzzer_play_json(ev["tones"].as<JsonArrayConst>());
}

// The server may own the hardware settings of this display (`settings.hw`). A differing, valid set is stored
// and the panel restarts with it; the same set again changes nothing, so this cannot loop. An invalid set is
// ignored (the server checks too). A wrong set stays harmless: WLAN and HA keep working, so it can be
// corrected in HA, and repeated crashes reset it (see hw_guard_boot).
static void apply_hw_from_server(JsonObjectConst hw) {
    HwConfig c = hw_cfg;
    hw_apply_json(c, hw);
    if (const char *why = hw_validate(c)) {
        Serial.printf("[HW] hardware settings from Home Assistant rejected: %s\n", why);
        return;
    }
    hw_set_managed(true);
    if (hw_equal(c, hw_cfg)) return;
    if (!hw_save(c)) { Serial.println("[HW] could not store the hardware settings from Home Assistant"); return; }
    Serial.println("[HW] hardware settings from Home Assistant changed, restarting");
    delay(300);
    ESP.restart();
}

static void on_layout(JsonObjectConst ev) {
    if (ota_running()) return;   // the update screen stays until the download is over
    if (ev["settings"]["hw"].is<JsonObjectConst>()) apply_hw_from_server(ev["settings"]["hw"].as<JsonObjectConst>());
    else hw_set_managed(false);
    click_enabled = ev["settings"]["click"] | false;
    int brightness = ev["settings"]["brightness"] | 0;
    if (brightness > 0) set_brightness(brightness);

    int rotation = constrain((int)(ev["settings"]["rotation"] | cur_rotation), 0, 3);
    bool rotated = rotation != cur_rotation;
    if (rotated) apply_rotation(rotation);

    int32_t rev  = ev["rev"] | 0;
    int     page = ev["page"]["index"] | 0;
    if (rev != built_rev || page != built_page || rotated) {
        int n = widgets_build(ev, ev["name"] | "Display");
        built_rev  = rev;
        built_page = page;
        status_screen = (n == 0);
        Serial.printf("[UI] page built: %d widgets, free heap %u, largest block %u, minimum since boot %u\n", n,
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
    }
}

static void enter_running() {
    state = RUNNING;
    status_screen = true;
    built_rev = -1; built_page = -1;
    last_ha_status = HA_CONNECTING;
    ui_show_running(cfg.ha_url);
    ha_client_on_frame(on_frame);
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

// A queued firmware update: blocks in ota_run() and restarts on success. On failure show why, then go back to
// normal operation; the connection is re-opened so the integration sends the layout again.
static void run_ota() {
    if (ota_run(cfg)) return;
    char msg[140];
    snprintf(msg, sizeof(msg), "Update fehlgeschlagen:\n%s", ota_last_error());
    screen_error(msg);
    for (uint32_t t0 = millis(); millis() - t0 < 4000;) { lv_task_handler(); ha_client_tick(); delay(5); }
    last_ha_status = HA_CONNECTING;
    ui_show_running(cfg.ha_url);
    status_screen = true;
    built_rev = -1; built_page = -1;
    ha_client_resync();
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\n[BOOT] ha-display fw %s (%s), reset reason %d, free heap %u\n", FW_VERSION, FW_VARIANT,
                  (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
    bool hw_dropped = hw_guard_boot();   // crash-loop recovery bookkeeping, may drop a bad hardware config
    bool hw_stored  = hw_load(hw_cfg);
    Serial.printf("[BOOT] hardware configuration: %s%s\n", hw_stored ? "stored" : "defaults",
                  hw_dropped ? " (reset after repeated crashes)" : "");
    hw_print(hw_cfg);
    buzzer_begin(hw_cfg.buzzer);
    Serial.println("[BOOT] stage: ota check");
    ota_boot_check();   // a new image that keeps failing is rolled back before it can do more harm
    Serial.println("[BOOT] stage: display");
    tft_init();
    hw_guard_poll();
    Serial.println("[BOOT] stage: touch");
    touch_init();
    hw_guard_poll();
    Serial.println("[BOOT] stage: lvgl");
    lvgl_init();
    Serial.println("[BOOT] stage: ui");
    ui_init();
    hw_guard_poll();
    build_ap_ssid();
    Serial.println("[BOOT] stage: storage");

    if (storage_load(cfg)) {
        Serial.printf("[BOOT] Credentials found, %d saved network(s)\n", cfg.wifi_count);
        enter_connecting_wifi();
    } else {
        Serial.println("[BOOT] No credentials, starting provisioning");
        enter_provisioning(false);
    }
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    hw_guard_poll();
    buzzer_tick();
    if (ota_running()) buzzer_stop();
    if (state != RUNNING) click_enabled = false;   // the setting arrives with the next layout frame
    // First boot without stored touch data: offer the calibration once, but only when the panel is up and
    // talking to Home Assistant (never while the hotspot, OTA or a connection attempt needs the device).
    if (touch_uncalibrated && !calibration_offered && state == RUNNING && !portal_overlay && !ota_running() &&
        millis() > 5000) {
        calibration_offered = true;
        calibrate_pending = true;
    }
    if (calibrate_pending) {
        calibrate_pending = false;
        run_calibration();
        lv_obj_invalidate(lv_scr_act());   // the layout objects are still there, only the pixels are gone
    }
    lv_task_handler();
    ota_loop_tick(WiFi.status() == WL_CONNECTED);

    // "Touch neu kalibrieren" from the setup page or the hotspot screen: run the calibration, then bring the
    // hotspot screen back. The page asks for the outcome afterwards.
    if ((state == PROVISIONING || portal_overlay) && hw_cfg.touch_on &&
        (portal_calibrate_req || provisioning_calibrate_requested())) {
        portal_calibrate_req = false;
        provisioning_calibrate_state(1);
        bool stored = run_calibration();
        provisioning_calibrate_state(stored ? 2 : 3);
        show_portal(portal_note);
        press_since = 0;
        return;
    }

    // Long press on an empty spot (not on an operable widget, not a drag): WLAN setup. Pages without any
    // empty spot are covered by the automatic hotspot after 90 s without WLAN.
    if (state != PROVISIONING && !portal_overlay && !ota_running() && touch_hold_ms() >= HOLD_MS) {
        bool empty = !press_moved && !widgets_input_at(press_x, press_y);
        press_since = 0;
        if (empty) {
            if (state == CONNECTING_WIFI) open_portal_overlay(true);
            else                          enter_provisioning(true);
            delay(5);
            return;
        }
    }

    // The hotspot next to the connection attempts: web page, cancel, timeout.
    if (portal_overlay) {
        provisioning_handle();
        if (provisioning_done()) {
            cfg = provisioning_config();
            storage_save(cfg);
            close_portal();
            offline_since = 0;
            auto_portal_ms = SAVE_GRACE_MS;   // a phone hotspot was probably off while it was set up
            enter_connecting_wifi();
            return;
        }
        if (portal_cancel || (portal_manual && millis() - portal_opened > PORTAL_TIMEOUT_MS)) {
            close_portal();
            offline_since = millis() ? millis() : 1;   // the next automatic hotspot is 90 s away again
            enter_connecting_wifi();
            return;
        }
    }

    switch (state) {

    case PROVISIONING:
        provisioning_handle();
        if (provisioning_done()) {
            cfg = provisioning_config();
            storage_save(cfg);
            close_portal();
            offline_since = 0;
            auto_portal_ms = SAVE_GRACE_MS;
            enter_connecting_wifi();
        } else if (portal_cancel || (portal_manual && millis() - portal_opened > PORTAL_TIMEOUT_MS)) {
            close_portal();
            if (WiFi.status() == WL_CONNECTED) enter_testing_ha();   // the connection was never dropped
            else                               enter_connecting_wifi();
        }
        break;

    case CONNECTING_WIFI:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Connected to '%s', IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            if (portal_overlay) close_portal();
            offline_since = 0;
            auto_portal_ms = AUTO_PORTAL_MS;
            enter_testing_ha();
        } else {
            if (!portal_overlay && millis() - offline_since >= auto_portal_ms) {
                auto_portal_ms = AUTO_PORTAL_MS;
                open_portal_overlay(false);
            } else if (millis() - connect_start > WIFI_TRY_MS) {
                // next saved network; after a full round without luck say so (not over the hotspot screen)
                if (++net_pos >= cfg.wifi_count) {
                    if (!wifi_fail_shown && !portal_overlay) {
                        char msg[128];
                        if (cfg.wifi_count == 1)
                            snprintf(msg, sizeof(msg), "WLAN '%s' nicht erreichbar.\nNeuer Versuch läuft...", cfg.wifi[0].ssid);
                        else
                            snprintf(msg, sizeof(msg), "Kein gespeichertes WLAN erreichbar.\nNeuer Versuch läuft...");
                        screen_error(msg);
                        wifi_fail_shown = true;
                    }
                    wifi_plan();   // new round (net_pos = 0)
                }
                wifi_begin();
            }
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
                    char msg[200];
                    if (missing)
                        strlcpy(msg, "Integration 'ESP32 Display' nicht gefunden.\nURL prüfen. Neuer Versuch läuft...", sizeof(msg));
                    else if (tls_last_error()[0])
                        snprintf(msg, sizeof(msg), "Sichere Verbindung fehlgeschlagen:\n%s\nNeuer Versuch läuft...", tls_last_error());
                    else
                        strlcpy(msg, "Home Assistant nicht erreichbar.\nURL prüfen. Neuer Versuch läuft...", sizeof(msg));
                    screen_error(msg);
                    ha_fail_shown = true;
                    ha_missing = missing;
                }
                next_ha_try = millis() + 10000;
            }
        }
        break;

    case RUNNING:
        if (WiFi.status() != WL_CONNECTED) {
            if (!wifi_lost_at) wifi_lost_at = millis() ? millis() : 1;
            if (millis() - wifi_lost_at > WIFI_LOST_MS) {   // the automatic reconnect did not help
                offline_since = wifi_lost_at;
                wifi_lost_at = 0;
                ha_client_stop();
                enter_connecting_wifi();
                break;
            }
        } else {
            wifi_lost_at = 0;
        }
        if (ota_pending()) { run_ota(); break; }
        ha_client_tick();
        widgets_tick();
        check_ha_status();
        break;
    }

    delay(5);
}
