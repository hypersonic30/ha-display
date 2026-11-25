#ifdef DISPLAY_LAB
#include "lab.h"
#include <lvgl.h>
#include "display.h"
#include "ha_client.h"

namespace {

struct Preset { uint8_t reg; uint8_t n; uint8_t p[4]; };
// Starting points, not recommendations: C5 = frame rate / inversion, C1 = display timing, C0 = panel driving.
const Preset PRESETS[] = {
    {0xC5, 1, {0x03, 0, 0, 0}},
    {0xC1, 3, {0x10, 0x10, 0x88, 0}},
    {0xC0, 4, {0x10, 0x3B, 0x00, 0x02}},
    {0xC5, 1, {0x01, 0, 0, 0}},
    {0xC5, 1, {0x05, 0, 0, 0}},
};
const int NPRE = sizeof(PRESETS) / sizeof(PRESETS[0]);

constexpr int COLS = 6;           // REG, P1..P4, N
uint8_t   val[COLS] = {0xC5, 0x03, 0, 0, 0, 1};   // val[5] = number of parameters sent (0..4)
lv_obj_t *lbl[COLS];
lv_obj_t *info;
lv_obj_t *root;
bool      closed = false;
int       bg = 0, preset = 0;
char      sent[96] = "";

void show() {
    char t[8];
    for (int i = 0; i < COLS; i++) {
        snprintf(t, sizeof(t), i == 5 ? "%u" : "%02X", (unsigned)val[i]);
        lv_label_set_text(lbl[i], t);
    }
}

void send() {
    uint8_t n = val[5] > 4 ? 4 : val[5];
    display_write_reg(val[0], val + 1, n);
    int o = snprintf(sent, sizeof(sent), "gesendet: %02X", (unsigned)val[0]);
    for (int i = 0; i < n; i++) o += snprintf(sent + o, sizeof(sent) - o, " %02X", (unsigned)val[1 + i]);
    lv_label_set_text(info, sent);
    Serial.printf("[LAB] %s\n", sent);
}

void step_cb(lv_event_t *e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);   // column * 1000 + (delta + 500)
    int col = code / 1000, d = (int)(code % 1000) - 500;
    int v = (int)val[col] + d;
    int hi = col == 5 ? 4 : 255;
    val[col] = (uint8_t)(v < 0 ? 0 : v > hi ? hi : v);
    show();
}

void send_cb(lv_event_t *) { send(); }
void close_cb(lv_event_t *) { closed = true; }
void bg_cb(lv_event_t *) {
    bg = (bg + 1) % 3;
    static const uint32_t c[3] = {0xFFFFFF, 0x808080, 0x000000};
    lv_obj_set_style_bg_color(root, lv_color_hex(c[bg]), 0);
    lv_obj_set_style_text_color(info, lv_color_hex(bg == 0 ? 0x000000 : 0xFFFFFF), 0);
    for (int i = 0; i < COLS; i++) lv_obj_set_style_text_color(lbl[i], lv_color_hex(bg == 0 ? 0x000000 : 0xFFFFFF), 0);
}
void preset_cb(lv_event_t *) {
    const Preset &p = PRESETS[preset];
    val[0] = p.reg; val[5] = p.n;
    for (int i = 0; i < 4; i++) val[1 + i] = p.p[i];
    preset = (preset + 1) % NPRE;
    show();
}

lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

}  // namespace

void lab_run() {
    closed = false;
    const int W = lv_disp_get_hor_res(NULL), H = lv_disp_get_ver_res(NULL);
    root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root, W, H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    bg = 0;

    info = lv_label_create(root);
    lv_label_set_text(info, "Register-Labor: Wert waehlen, Senden");
    lv_obj_set_style_text_color(info, lv_color_hex(0x000000), 0);
    lv_obj_set_pos(info, 6, 4);

    const int cw = W / COLS, bh = (H - 100) / 5;
    static const int D[4] = {16, 1, -1, -16};
    static const char *T[4] = {"+16", "+1", "-1", "-16"};
    static const char *H_[COLS] = {"REG", "P1", "P2", "P3", "P4", "Anz"};
    for (int c = 0; c < COLS; c++) {
        lv_obj_t *h = lv_label_create(root);
        lv_label_set_text(h, H_[c]);
        lv_obj_set_style_text_color(h, lv_color_hex(0x606060), 0);
        lv_obj_set_pos(h, c * cw + 6, 22);
        lbl[c] = lv_label_create(root);
        lv_obj_set_style_text_color(lbl[c], lv_color_hex(0x000000), 0);
        lv_obj_set_pos(lbl[c], c * cw + 6, 38);
        for (int r = 0; r < 4; r++) {
            if (c == 5 && (D[r] == 16 || D[r] == -16)) continue;
            button(root, T[r], c * cw + 2, 56 + r * bh, cw - 4, bh - 4, step_cb, (void *)(intptr_t)(c * 1000 + 500 + D[r]));
        }
    }
    const int by = H - 40, bw = W / 4;
    button(root, "Senden", 2, by, bw - 4, 36, send_cb, nullptr);
    button(root, "Vorgabe", bw + 2, by, bw - 4, 36, preset_cb, nullptr);
    button(root, "Hintergrund", 2 * bw + 2, by, bw - 4, 36, bg_cb, nullptr);
    button(root, "Schliessen", 3 * bw + 2, by, bw - 4, 36, close_cb, nullptr);
    show();

    while (!closed) {
        lv_task_handler();
        ha_client_tick();
        delay(5);
    }
    lv_obj_del(root);
    lv_obj_invalidate(lv_scr_act());
}
#endif
