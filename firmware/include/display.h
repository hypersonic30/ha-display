#pragma once
#include <Arduino.h>
#include "hw.h"

// Thin layer over LovyanGFX: the panel, its SPI pins and the XPT2046 touch chip are configured from a
// HwConfig at runtime. Everything else (LVGL, the screens) only uses these functions.

bool     display_begin(const HwConfig &hw);          // false: no config applied (never happens with a valid one)
int      display_width();                            // after rotation
int      display_height();
void     display_set_rotation(int rot);              // 0..3, 0 = landscape 480x320 (the layout's rotation)
void     display_flush(int x1, int y1, int x2, int y2, const uint16_t *rgb565);
void     display_fill_black();
void     display_fill(uint16_t rgb565);

// Touch: x/y in screen coordinates of the current rotation. False = not pressed.
bool     display_touch(uint16_t &x, uint16_t &y);

// 0..100 percent; the backlight pin is PWM driven.
void     display_set_brightness(int percent);

// Touch calibration: the 4 raw corner values (8 x uint16_t) are independent of the rotation.
void     display_apply_calibration(const uint16_t cal[8]);
// Shows the corner markers and waits for taps; false if nobody touches within timeout_ms (nothing changed).
bool     display_calibrate(uint16_t cal[8], uint32_t timeout_ms);

// Small helpers for the boot/calibration texts (ASCII).
void     display_text_centered(const char *text, int x, int y, int font_size /*1 = small, 2 = bigger*/, bool dim);

// Colour bars red/green/blue/white (1 s each) + controller ID read-back in the serial log.
void     display_test();
void     display_print_ids();

// Raw register write (test builds): command byte plus up to 4 parameter bytes.
void     display_write_reg(uint8_t reg, const uint8_t *params, int n);
