#pragma once
#include <lvgl.h>
#include <ArduinoJson.h>

// Rebuilds the whole screen from a layout frame ({widgets, page, pager, ...}); returns the number of
// widgets on this page (without the pager). With an empty page a hint with `device_name` is shown.
int  widgets_build(JsonObjectConst frame, const char *device_name);

// `key` is "entity_id" or "entity_id|name" (see "States" in docs/protocol.md).
void widgets_update(const char *key, const char *state);

// {"type":"options","widget":id,"items":[...]} for dropdown / roller.
void widgets_options(JsonObjectConst frame);

// {"type":"time","epoch":..,"offset":..}: sets the local clock.
void widgets_time(JsonObjectConst frame);

// Call from loop(): advances the clock widgets.
void widgets_tick();

// Forget all widget handles without deleting objects. Must be called whenever
// something else cleans the screen (ui.cpp does so before drawing a status screen).
void widgets_reset();

// True if (x, y) lies on (or close to) something that takes input: an operable widget, the page switcher or
// an open dropdown list. Passive widgets (label, gauge, bar, led, clock, panel) and the background are
// "empty": a long press there opens the WLAN setup.
bool widgets_input_at(int x, int y);
