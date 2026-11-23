#pragma once
#include <lvgl.h>
#include <ArduinoJson.h>

void widgets_build(JsonArrayConst layout);
void widgets_update(const char *entity_id, const char *state, JsonObjectConst attrs);
void widgets_clear();
