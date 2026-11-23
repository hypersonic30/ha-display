#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "secrets.h"

// ── Display ──────────────────────────────────────────────────────────────────

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void display_init() {
    tft.begin();
    tft.setRotation(1);  // landscape
    tft.fillScreen(TFT_BLACK);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, 220);  // backlight ~86 %

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

// ── UI helpers ────────────────────────────────────────────────────────────────

static lv_obj_t *status_label;

static void ui_boot_screen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32 Display");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4fc3f7), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Verbinde mit WLAN...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 60);
}

static void set_status(const char *msg) {
    if (status_label) lv_label_set_text(status_label, msg);
    lv_task_handler();
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    display_init();
    ui_boot_screen();
    lv_task_handler();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        lv_task_handler();
    }
    Serial.println();
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    char msg[64];
    snprintf(msg, sizeof(msg), "IP: %s", WiFi.localIP().toString().c_str());
    set_status(msg);

    // TODO: connect to HA WebSocket
    // TODO: receive layout JSON from HA
    // TODO: render widgets based on layout
}

void loop() {
    lv_task_handler();
    delay(5);
}
