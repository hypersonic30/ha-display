#include "display.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── Panel with runtime-selectable controller ─────────────────────────────────
// The init sequences and the memory-access (rotation) tables are the ones TFT_eSPI used before 0.7.0, so a panel
// that worked there looks the same here. The colour-order bit is added by the base class from cfg.rgb_order.

namespace {

enum { DLY = 0x80 };   // "a delay byte follows" flag of the command lists

const uint8_t INIT_ILI9481[] = {
    0x11, 0 + DLY, 20,                                   // sleep out
    0xD0, 3, 0x07, 0x42, 0x18,
    0xD1, 3, 0x00, 0x07, 0x10,
    0xD2, 2, 0x01, 0x02,
    0xC0, 5, 0x10, 0x3B, 0x00, 0x02, 0x11,
    0xC5, 1, 0x03,
    0xC8, 12, 0x00, 0x32, 0x36, 0x45, 0x06, 0x16, 0x37, 0x75, 0x77, 0x54, 0x0C, 0x00,
    0x00, 0 + DLY, 120,                                  // NOP: wait before the display is switched on
    0x29, 0 + DLY, 20,
    0xFF, 0xFF};

const uint8_t INIT_ILI9488[] = {
    0xE0, 15, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F,
    0xE1, 15, 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F,
    0xC0, 2, 0x17, 0x15,
    0xC1, 1, 0x41,
    0xC5, 3, 0x00, 0x12, 0x80,
    0xB0, 1, 0x00,
    0xB1, 1, 0xA0,
    0xB4, 1, 0x02,
    0xB6, 3, 0x02, 0x02, 0x3B,
    0xB7, 1, 0xC6,
    0xF7, 4, 0xA9, 0x51, 0x2C, 0x82,
    0x11, 0 + DLY, 120,
    0x29, 0 + DLY, 25,
    0xFF, 0xFF};

const uint8_t INIT_ILI9486[] = {
    0x01, 0 + DLY, 120,                                  // software reset
    0x11, 0 + DLY, 120,
    0xC0, 2, 0x0E, 0x0E,
    0xC1, 2, 0x41, 0x00,
    0xC2, 1, 0x55,
    0xC5, 4, 0x00, 0x00, 0x00, 0x00,
    0xE0, 15, 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98, 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00,
    0xE1, 15, 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
    0x29, 0 + DLY, 150,
    0xFF, 0xFF};

const uint8_t INIT_ST7796[] = {
    0x01, 0 + DLY, 120,
    0x11, 0 + DLY, 120,
    0xF0, 1, 0xC3,
    0xF0, 1, 0x96,
    0xB4, 1, 0x01,
    0xB6, 3, 0x80, 0x02, 0x3B,
    0xE8, 8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33,
    0xC1, 1, 0x06,
    0xC2, 1, 0xA7,
    0xC5, 1, 0x18,
    0x00, 0 + DLY, 120,
    0xE0, 14, 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B,
    0xE1, 14, 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B,
    0x00, 0 + DLY, 120,
    0xF0, 1, 0x3C,
    0xF0, 1, 0x69,
    0x00, 0 + DLY, 120,
    0x29, 0 + DLY, 20,
    0xFF, 0xFF};

class Panel_Dyn : public lgfx::Panel_ILI948x {
public:
    uint8_t kind = PANEL_ILI9481;

    // 16 bit over SPI only works on the ST7796; the ILI948x family needs 18 bit (3 bytes per pixel).
    void setColorDepth_impl(lgfx::color_depth_t depth) override {
        bool two = kind == PANEL_ST7796 && ((int)depth & lgfx::color_depth_t::bit_mask) <= 16;
        _write_depth = two ? lgfx::rgb565_2Byte : lgfx::rgb888_3Byte;
        _read_depth  = lgfx::rgb888_3Byte;
    }
    uint8_t getColMod(uint8_t bpp) const override {
        return (kind == PANEL_ST7796 && bpp <= 16) ? RGB565_2BYTE : RGB888_3BYTE;
    }

protected:
    const uint8_t *getInitCommands(uint8_t listno) const override {
        if (listno != 0) return nullptr;
        switch (kind) {
        case PANEL_ILI9488: return INIT_ILI9488;
        case PANEL_ILI9486: return INIT_ILI9486;
        case PANEL_ST7796:  return INIT_ST7796;
        default:            return INIT_ILI9481;
        }
    }

    // Same values as TFT_eSPI's rotation tables (without the colour bit). Index = internal rotation 0..3.
    uint8_t getMadCtl(uint8_t r) const override {
        static const uint8_t t9481[4] = {MAD_HF, MAD_MV, MAD_VF, (uint8_t)(MAD_MV | MAD_HF | MAD_VF)};
        static const uint8_t tstd[4]  = {MAD_MX, MAD_MV, MAD_MY, (uint8_t)(MAD_MX | MAD_MY | MAD_MV)};
        return (kind == PANEL_ILI9481 ? t9481 : tstd)[r & 3];
    }
};

class Display : public lgfx::LGFX_Device {
public:
    Panel_Dyn           panel;
    lgfx::Bus_SPI       bus;
    lgfx::Touch_XPT2046 touch;

    void setup(const HwConfig &hw) {
        {
            auto c = bus.config();
            c.spi_host    = SPI3_HOST;                    // VSPI
            c.spi_mode    = 0;
            c.freq_write  = (uint32_t)hw.write_mhz * 1000000UL;
            c.freq_read   = (uint32_t)hw.read_mhz * 1000000UL;
            c.spi_3wire   = false;
            c.use_lock    = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk    = hw.sclk;
            c.pin_mosi    = hw.mosi;
            c.pin_miso    = hw.miso;
            c.pin_dc      = hw.dc;
            bus.config(c);
            panel.setBus(&bus);
        }
        {
            panel.kind = hw.panel;
            auto c = panel.config();
            c.pin_cs           = hw.cs;
            c.pin_rst          = hw.rst;
            c.pin_busy         = -1;
            c.panel_width      = c.memory_width  = 320;
            c.panel_height     = c.memory_height = 480;
            c.offset_x = c.offset_y = 0;
            c.offset_rotation  = 0;
            c.dummy_read_pixel = 8;
            c.dummy_read_bits  = 1;
            c.readable         = true;
            c.invert           = hw.invert;
            c.rgb_order        = !hw.bgr;
            c.dlen_16bit       = false;
            c.bus_shared       = true;                    // the touch chip sits on the same SPI bus
            panel.config(c);
        }
        if (hw.touch_on) {
            auto c = touch.config();
            c.x_min = 300; c.x_max = 3600;                // rough defaults until a calibration is stored
            c.y_min = 300; c.y_max = 3600;
            c.pin_int         = hw.touch_irq;
            c.bus_shared      = true;
            c.offset_rotation = 0;
            c.spi_host        = SPI3_HOST;
            c.freq            = 2500000;
            c.pin_sclk        = hw.sclk;
            c.pin_mosi        = hw.mosi;
            c.pin_miso        = hw.miso;
            c.pin_cs          = hw.touch_cs;
            touch.config(c);
            panel.setTouch(&touch);
        }
        setPanel(&panel);
    }
};

Display tft;
HwConfig cur;
bool     touch_present = false;

// Typed colours: the raw value depends on the panel's write depth (24 bit on the ILI948x over SPI).
lgfx::rgb888_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lgfx::rgb888_t(r, g, b); }

}  // namespace

// ── API ──────────────────────────────────────────────────────────────────────

bool display_begin(const HwConfig &hw) {
    cur = hw;
    touch_present = hw.touch_on;
    tft.setup(hw);
    bool ok = tft.init();
    if (hw.bl >= 0) ledcAttach(hw.bl, 5000, 8);
    return ok;
}

int  display_width()  { return tft.width(); }
int  display_height() { return tft.height(); }

void display_set_rotation(int rot) {
    tft.setRotation((1 + rot) & 3);        // layout rotation 0 = landscape = internal rotation 1
}

void display_flush(int x1, int y1, int x2, int y2, const uint16_t *px) {
    tft.startWrite();
    tft.pushImage(x1, y1, x2 - x1 + 1, y2 - y1 + 1, (const lgfx::rgb565_t *)px);
    tft.endWrite();
}

void display_fill_black() { tft.fillScreen(0); }

bool display_touch(uint16_t &x, uint16_t &y) {
    if (!touch_present) return false;
    int32_t tx, ty;
    if (!tft.getTouch(&tx, &ty)) return false;
    x = tx < 0 ? 0 : (uint16_t)tx;
    y = ty < 0 ? 0 : (uint16_t)ty;
    return true;
}

void display_set_brightness(int percent) {
    if (cur.bl < 0) return;
    percent = constrain(percent, 1, 100);
    int duty = percent * 255 / 100;
    ledcWrite(cur.bl, cur.bl_invert ? 255 - duty : duty);
}

void display_apply_calibration(const uint16_t cal[8]) {
    if (!touch_present) return;
    uint16_t c[8];
    memcpy(c, cal, sizeof(c));
    tft.setTouchCalibrate(c);
}

bool display_calibrate(uint16_t cal[8], uint32_t timeout_ms) {
    if (!touch_present) return false;
    const uint32_t deadline = millis() + timeout_ms;
    const int r = 15;
    auto rot = tft.getRotation();
    tft.setRotation(0);                   // the touch chip's own orientation (offset rotations are zero)
    uint16_t raw[8];
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++) {
        int px = (tft.width() - 1) * ((i >> 1) & 1);
        int py = (tft.height() - 1) * (i & 1);
        tft.fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, 0);
        tft.drawLine(px - r, py, px + r, py, rgb(255, 0, 255));
        tft.drawLine(px, py - r, px, py + r, rgb(255, 0, 255));
        tft.drawCircle(px, py, r / 2, rgb(255, 0, 255));
        delay(500);
        int32_t sx = 0, sy = 0;
        for (int j = 0; j < 8 && ok; j++) {
            lgfx::touch_point_t a, b;
            for (;;) {
                if (millis() > deadline) { ok = false; break; }
                if (!tft.getTouchRaw(&a)) { delay(3); continue; }
                delay(10);
                if (tft.getTouchRaw(&b) && abs(a.x - b.x) <= 20 && abs(a.y - b.y) <= 20) break;
            }
            if (!ok) break;
            sx += a.x + b.x;
            sy += a.y + b.y;
        }
        if (!ok) break;
        raw[i * 2] = sx >> 4;
        raw[i * 2 + 1] = sy >> 4;
        tft.fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, 0);
        lgfx::touch_point_t t;
        for (uint32_t t0 = millis(); tft.getTouchRaw(&t) && millis() - t0 < 3000;) delay(5);   // finger off
    }
    tft.setRotation(rot);
    if (!ok) return false;
    memcpy(cal, raw, sizeof(raw));
    tft.setTouchCalibrate(raw);
    return true;
}

void display_text_centered(const char *text, int x, int y, int font_size, bool dim) {
    tft.setTextColor(dim ? rgb(128, 128, 128) : rgb(255, 255, 255), 0);
    tft.setTextDatum(lgfx::middle_center);
    tft.setFont(font_size >= 2 ? (const lgfx::IFont *)&lgfx::fonts::Font4 : (const lgfx::IFont *)&lgfx::fonts::Font2);
    tft.drawString(text, x, y);
}

void display_print_ids() {
    struct Q { uint8_t cmd; uint8_t n; const char *name; };
    static const Q qs[] = {{0x04, 4, "RDDID 0x04"}, {0xD3, 4, "ID4 0xD3 (9341/9488)"}, {0xBF, 6, "ILI9481 0xBF"},
                           {0x09, 5, "STATUS 0x09"}, {0x0A, 1, "POWER 0x0A"}, {0x0B, 1, "MADCTL 0x0B"},
                           {0x0C, 1, "PIXFMT 0x0C"}};
    Serial.printf("[HW] controller read-back (all 00/FF = the module does not answer reads, normal for many):\n");
    for (const Q &q : qs) {
        Serial.printf("[HW]   %-22s:", q.name);
        for (uint8_t i = 0; i < q.n; i++) Serial.printf(" %02X", (unsigned)(tft.panel.readCommand(q.cmd, i, 1) & 0xFF));
        Serial.println();
    }
    Serial.println("[HW]   0xBF -> 'xx 02 04 94 81 xx' = ILI9481 | 0xD3 -> '00 00 94 88' = ILI9488 | 77 96 = ST7796 | 94 86 = ILI9486");
}

void display_test() {
    display_print_ids();
    static const struct { uint8_t r, g, b; const char *n; } bars[] = {
        {255, 0, 0, "red"}, {0, 255, 0, "green"}, {0, 0, 255, "blue"}, {255, 255, 255, "white"}};
    for (const auto &b : bars) {
        Serial.printf("[HW] test bar %s\n", b.n);
        tft.fillScreen(rgb(b.r, b.g, b.b));
        delay(1000);
    }
    tft.fillScreen(0);
    Serial.println("[HW] test done: expected red, green, blue, white (other colours = colour order / inversion)");
}
