#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage.h"

// Called when an entity state changes.
// entity_id: e.g. "sensor.temperature"
// state:     e.g. "21.5"
// attributes: full JSON object (may be nullptr)
typedef void (*state_cb_t)(const char *entity_id,
                           const char *state,
                           JsonObjectConst attributes);

// Called when the layout JSON was fetched from the integration.
typedef void (*layout_cb_t)(JsonArrayConst widgets);

void ha_client_begin(const Config &cfg, state_cb_t on_state, layout_cb_t on_layout);
void ha_client_tick();
bool ha_client_connected();

// Send a service call (e.g. from a button press on the display).
// domain:  "light"
// service: "toggle"
// entity:  "light.wohnzimmer"
void ha_client_call_service(const char *domain,
                            const char *service,
                            const char *entity_id);

// Subscribe to a list of entity IDs (called after layout is loaded).
void ha_client_subscribe(const char **entity_ids, size_t count);
