#pragma once
#include <Arduino.h>

// Display / touch hardware that can be set at runtime (portal, section "Hardware"). The defaults are the
// author's board (see docs/hardware.md); the build variant (env ili9481 / ili9488) only picks the default
// controller, so OTA updates between 0.6.x and 0.7.x keep the right panel.
enum PanelKind : uint8_t { PANEL_ILI9481 = 0, PANEL_ILI9488 = 1, PANEL_ILI9486 = 2, PANEL_ST7796 = 3, PANEL_COUNT };

struct HwConfig {
    uint8_t ver;          // layout of this struct in NVS, bump when it changes
    uint8_t panel;        // PanelKind
    int8_t  mosi, sclk, miso;   // SPI bus shared by the panel and the touch chip
    int8_t  cs, dc, rst;        // rst = -1: no reset pin
    int8_t  bl;                 // backlight, -1: none
    uint8_t bl_invert;          // 1 = the backlight is on at LOW
    uint8_t write_mhz;          // panel SPI write clock, 1..40
    uint8_t read_mhz;           // panel SPI read clock, 1..20
    uint8_t touch_on;           // XPT2046 present
    int8_t  touch_cs;
    int8_t  touch_irq;          // -1 = poll the pressure over SPI (works without the IRQ wire)
    uint8_t bgr;                // 1 = colour order BGR (usual for these modules), 0 = RGB
    uint8_t invert;             // 1 = display inversion on
    int8_t  buzzer;             // buzzer transistor (tone via LEDC), -1 = none. Added in layout version 2: keep it last
};

const char *hw_panel_name(uint8_t panel);

// Recommended clock / colour flags for a controller (what the portal fills in when the driver is changed).
void hw_preset(HwConfig &cfg, uint8_t panel);
void hw_defaults(HwConfig &cfg);                 // author's pinout + the build variant's controller

bool hw_load(HwConfig &cfg);                     // false: nothing (valid) stored, cfg = defaults
bool hw_save(const HwConfig &cfg);
void hw_reset_stored();                          // forget the stored configuration -> defaults at next boot

// nullptr = fine, else a short German reason. Checks ranges, input-only/flash/UART pins and duplicates.
const char *hw_validate(const HwConfig &cfg);

void hw_print(const HwConfig &cfg);              // boot log

// Recovery: a wrong pin/driver can leave the screen blank or crash the firmware before the portal is
// reachable, so the stored hardware configuration is dropped automatically when
//  - the device panics or hits a watchdog 3 times in a row within its first 20 s, or
//  - the BOOT key (GPIO0) is held for 3 s during the first 30 s after power-up.
bool hw_guard_boot();     // first thing in setup(); true if the stored configuration was just dropped
void hw_guard_poll();     // call from loop() and between boot stages
