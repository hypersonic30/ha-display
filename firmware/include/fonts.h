#pragma once
#include <lvgl.h>

// Montserrat Medium with Latin-1 + Euro (see tools/gen_fonts.py)
extern const lv_font_t ui_font_12;
extern const lv_font_t ui_font_14;
extern const lv_font_t ui_font_16;
extern const lv_font_t ui_font_20;
extern const lv_font_t ui_font_24;

// Digits, space and "-./:" only, for the clock widget
extern const lv_font_t ui_clock_32;
extern const lv_font_t ui_clock_48;
