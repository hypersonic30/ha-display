#if 1  /* enable this file */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 1, 8, 16, 32 */
#define LV_COLOR_DEPTH 16

/* ILI9488/ILI9481 are 480x320, landscape */
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 320

/* DPI (pixels per inch) */
#define LV_DPI_DEF 130

/* Memory: use the ESP32 heap (32 widgets x several objects outgrow a fixed pool) */
#define LV_MEM_CUSTOM 1

/* Without a tick source LVGL's timers (screen refresh, animations) never fire */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Log */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Widgets */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0

/* Extra widgets */
#define LV_USE_METER      1
#define LV_USE_MSGBOX     1
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_SPAN       0

/* Fonts */
/* UI fonts with Latin-1 + Euro live in src/fonts (ui_font_NN); only the theme default stays built in */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Animation */
#define LV_USE_ANIMATION 1

/* Asserts */
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

#endif /* LV_CONF_H */
#endif /* enable */
