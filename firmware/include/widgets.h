#pragma once
#include <lvgl.h>
#include <ArduinoJson.h>

// Rebuilds the whole screen from a layout frame ({widgets, page, pager, ...}); returns the number of
// widgets on this page (without the pager). With an empty page a hint with `device_name` is shown.
int  widgets_build(JsonObjectConst frame, const char *device_name);

void widgets_update(const char *entity_id, const char *state);

// Forget all widget handles without deleting objects. Must be called whenever
// something else cleans the screen (ui.cpp does so before drawing a status screen).
void widgets_reset();
