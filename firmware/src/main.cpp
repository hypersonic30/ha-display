#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "storage.h"
#include "provisioning.h"
#include "ui.h"
#include "ha_client.h"
#include "widgets.h"
#include "ota.h"

// ── Display, touch & LVGL ─────────────────────────────────────────────────────

static const int DEFAULT_BRIGHTNESS = 86;   // percent, until the layout says otherwise
static const uint32_t HOLD_MS       = 3000;

TFT_eSPI tft = TFT_eSPI();
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
static bool touch_uncalibrated = false;  // running on default data, nothing stored for this rotation yet
static bool calibration_offered = false; // the one automatic offer per boot has been made

static int tft_rotation(int r) { return (1 + r) % 4; }   // 0 = the original TFT_eSPI rotation 1

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
        if (!press_since) {
            press_since = millis() ? millis() : 1;
            press_x = x; press_y = y; press_moved = false;
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

static void set_brightness(int percent) {
    percent = constrain(percent, 1, 100);
    ledcWrite(TFT_BL, percent * 255 / 100);
}

#ifdef DIAG_BUILD
// Diagnostic builds only: print what the controller answers to its ID commands (MISO is shared with the touch
// chip, so an all-0x00 / all-0xFF answer proves nothing) and paint colour bars so a working panel is obvious.
static void diag_display() {
    struct Q { uint8_t cmd; uint8_t n; const char *name; };
    static const Q qs[] = {{0x04, 4, "RDDID 0x04"}, {0xD3, 4, "ID4 0xD3 (9341/9488)"}, {0xBF, 6, "ILI9481 0xBF"},
                           {0xDA, 1, "ID1 0xDA"}, {0xDB, 1, "ID2 0xDB"}, {0xDC, 1, "ID3 0xDC"},
                           {0x09, 5, "STATUS 0x09"}, {0x0A, 1, "POWER 0x0A"}, {0x0B, 1, "MADCTL 0x0B"},
                           {0x0C, 1, "PIXFMT 0x0C"}};
    Serial.printf("[DIAG] driver %s, SPI %u Hz, read %u Hz, panel %dx%d\n", FW_VARIANT, (unsigned)SPI_FREQUENCY,
                  (unsigned)SPI_READ_FREQUENCY, tft.width(), tft.height());
    for (const Q &q : qs) {
        Serial.printf("[DIAG] %-22s:", q.name);
        for (uint8_t i = 0; i < q.n; i++) Serial.printf(" %02X", tft.readcommand8(q.cmd, i));
        Serial.println();
    }
    Serial.println("[DIAG] read: 0xBF -> 'xx 02 04 94 81 xx' = ILI9481 | 0xD3 -> '00 00 94 88' = ILI9488 | "
                   "0x04 or 0xD3 containing 77 96 = ST7796 | '00 93 41' = ILI9341 | 94 86 = ILI9486");
    Serial.println("[DIAG] all 00 or all FF = no answer (MISO not wired to the panel, or the panel ignores reads): "
                   "that is normal for many SPI modules; judge by the colour bars");
    static const struct { uint16_t c; const char *n; } bars[] = {{TFT_RED, "red"}, {TFT_GREEN, "green"},
                                                                  {TFT_BLUE, "blue"}, {TFT_WHITE, "white"}};
    for (const auto &b : bars) {
        Serial.printf("[DIAG] bar %s\n", b.n);
        tft.fillScreen(b.c);
        delay(1000);
    }
    tft.fillScreen(TFT_BLACK);
    Serial.println("[DIAG] bars done: expected red, green, blue, white. Other colours = colour order/inversion.");
}
#endif

static void tft_init() {
    cur_rotation = storage_load_rotation();
    tft.begin();
    tft.setRotation(tft_rotation(cur_rotation));
    tft.fillScreen(TFT_BLACK);
    ledcAttach(TFT_BL, 5000, 8);
    set_brightness(DEFAULT_BRIGHTNESS);
#ifdef DIAG_BUILD
    diag_display();
#endif
}

// Held for HOLD_MS right after power-up: calibrate the touch of the current rotation again.
static bool boot_hold_requested() {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("Touch 3 s halten: Touch neu kalibrieren", tft.width() / 2, tft.height() / 2 - 10, 2);

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

// Rough XPT2046 data per rotation (TFT_eSPI example values for 480x320, flipped for the 180 degree
// rotations). Good enough to hit big targets; a real calibration replaces them.
static void default_touch_cal(int r, uint16_t cal[5]) {
    static const uint16_t base[2][5] = {
        {275, 3620, 264, 3532, 1},   // landscape: axes swapped
        {300, 3600, 300, 3600, 0},   // portrait
    };
    const uint16_t *b = base[r & 1];
    for (int i = 0; i < 4; i++) cal[i] = b[i];
    cal[4] = b[4] | ((r & 2) ? 6 : 0);   // rotations 2 and 3: both axes inverted
}

// Touch pressure above this counts as a press (TFT_eSPI uses Z_THRESHOLD/2 for the corners as well).
static const uint16_t CAL_Z = 175;

// Corner markers like TFT_eSPI::calibrateTouch (same maths) but with a deadline: returns false if nobody
// touches within `timeout_ms` of the start (display blank / no touch), so a dead panel never blocks boot.
static bool calibrate_corners(uint16_t cal[5], uint32_t timeout_ms) {
    const uint8_t size = 15;
    const int16_t w = tft.width(), h = tft.height();
    const uint32_t deadline = millis() + timeout_ms;
    int32_t v[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    for (uint8_t i = 0; i < 4; i++) {
        tft.fillRect(0, 0, size + 1, size + 1, TFT_BLACK);
        tft.fillRect(0, h - size - 1, size + 1, size + 1, TFT_BLACK);
        tft.fillRect(w - size - 1, 0, size + 1, size + 1, TFT_BLACK);
        tft.fillRect(w - size - 1, h - size - 1, size + 1, size + 1, TFT_BLACK);
        int16_t cx = (i == 2 || i == 3) ? w - 1 : 0, cy = (i == 1 || i == 3) ? h - 1 : 0;
        int16_t dx = cx ? -size : size, dy = cy ? -size : size;
        tft.drawLine(cx, cy, cx + dx, cy, TFT_MAGENTA);
        tft.drawLine(cx, cy, cx, cy + dy, TFT_MAGENTA);
        tft.drawLine(cx, cy, cx + dx, cy + dy, TFT_MAGENTA);

        if (i > 0) delay(1000);   // give the finger time to leave the previous corner
        for (uint8_t j = 0; j < 8; j++) {
            for (;;) {
                if (millis() > deadline) return false;
                if (tft.getTouchRawZ() > CAL_Z) break;
                delay(5);
            }
            delay(10);
            uint16_t x, y;
            tft.getTouchRaw(&x, &y);
            v[i * 2] += x; v[i * 2 + 1] += y;
            delay(10);
        }
        v[i * 2] /= 8; v[i * 2 + 1] /= 8;
    }

    bool rotate = abs(v[0] - v[2]) > abs(v[1] - v[3]);
    int32_t x0, x1, y0, y1;
    if (rotate) { x0 = (v[1] + v[3]) / 2; x1 = (v[5] + v[7]) / 2; y0 = (v[0] + v[4]) / 2; y1 = (v[2] + v[6]) / 2; }
    else        { x0 = (v[0] + v[2]) / 2; x1 = (v[4] + v[6]) / 2; y0 = (v[1] + v[5]) / 2; y1 = (v[3] + v[7]) / 2; }
    bool inv_x = false, inv_y = false;
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; inv_x = true; }
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; inv_y = true; }
    x1 -= x0; y1 -= y0;
    if (x0 == 0) x0 = 1;
    if (x1 == 0) x1 = 1;
    if (y0 == 0) y0 = 1;
    if (y1 == 0) y1 = 1;
    cal[0] = x0; cal[1] = x1; cal[2] = y0; cal[3] = y1;
    cal[4] = (rotate ? 1 : 0) | (inv_x ? 2 : 0) | (inv_y ? 4 : 0);
    return true;
}

// Blocking for at most about a minute: corner markers, then store the result for the current rotation. The
// XPT2046 mapping differs per rotation, so each of the four has its own data. Without any touch it gives
// up, keeps the default data and does not store anything (the next boot offers it again).
static void run_calibration() {
    uint16_t cal[5];
    Serial.println("[TOUCH] calibration started");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Touch kalibrieren", tft.width() / 2, tft.height() / 2 - 24, 4);
    tft.drawCentreString("Markierungen an den Ecken antippen", tft.width() / 2, tft.height() / 2 + 16, 2);
    delay(1500);
    if (calibrate_corners(cal, 60000)) {
        storage_save_touch(cur_rotation, cal);
        tft.setTouch(cal);
        touch_uncalibrated = false;
        Serial.println("[TOUCH] calibration stored");
    } else {
        default_touch_cal(cur_rotation, cal);
        tft.setTouch(cal);
        Serial.println("[TOUCH] no touch within 60 s, keeping default data");
    }
    tft.fillScreen(TFT_BLACK);
    press_since = 0;
}

// Never blocks without a user action: no stored data means default data and the normal start; the
// calibration is offered later (see loop()). Holding the screen at power-up still forces it.
static void touch_init() {
    uint16_t cal[5];
    bool have = storage_load_touch(cur_rotation, cal);
    if (!have) {
        default_touch_cal(cur_rotation, cal);
        touch_uncalibrated = true;
        Serial.println("[TOUCH] no stored calibration, using defaults");
    }
    tft.setTouch(cal);

    if (boot_hold_requested()) run_calibration();
    tft.fillScreen(TFT_BLACK);
}

static void lvgl_init() {
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 32);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = tft.width();
    disp_drv.ver_res  = tft.height();
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
    tft.setRotation(tft_rotation(r));
    disp_drv.hor_res = tft.width();
    disp_drv.ver_res = tft.height();
    lv_disp_drv_update(disp, &disp_drv);

    uint16_t cal[5];
    if (storage_load_touch(r, cal)) { tft.setTouch(cal); touch_uncalibrated = false; }
    else {
        default_touch_cal(r, cal);
        tft.setTouch(cal);                  // usable until the calibration below has run
        touch_uncalibrated = true;
        calibrate_pending = true;           // from loop(): not inside a WebSocket callback
    }
    tft.fillScreen(TFT_BLACK);
    press_since = 0;
    Serial.printf("[UI] rotation %d (%dx%d)\n", r, (int)tft.width(), (int)tft.height());
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

static void show_portal(const char *note) {
    status_screen = false;
    built_rev = -1; built_page = -1;
    portal_cancel = false;
    portal_opened = millis();
    ui_show_provisioning(ap_ssid, provisioning_ap_pass(), "192.168.4.1",
                         portal_cancelable ? on_portal_cancel : nullptr, note);
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
}

static void on_layout(JsonObjectConst ev) {
    if (ota_running()) return;   // the update screen stays until the download is over
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
    Serial.printf("[BOOT] TFT pins MOSI=%d SCLK=%d MISO=%d CS=%d DC=%d RST=%d BL=%d, touch CS=%d\n",
                  TFT_MOSI, TFT_SCLK, TFT_MISO, TFT_CS, TFT_DC, TFT_RST, TFT_BL, TOUCH_CS);
    Serial.println("[BOOT] stage: ota check");
    ota_boot_check();   // a new image that keeps failing is rolled back before it can do more harm
    Serial.println("[BOOT] stage: display");
    tft_init();
    Serial.println("[BOOT] stage: touch");
    touch_init();
    Serial.println("[BOOT] stage: lvgl");
    lvgl_init();
    Serial.println("[BOOT] stage: ui");
    ui_init();
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
            enter_testing_ha();
        } else {
            if (!portal_overlay && millis() - offline_since >= AUTO_PORTAL_MS) {
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
