#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage.h"

#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

enum HaStatus {
    HA_CONNECTING,      // socket not up / no layout yet
    HA_AUTH_FAILED,     // device code rejected, client halted (no retries)
    HA_PROTOCOL_ERROR,  // other refusal, text in ha_client_error()
    HA_OK               // layout received
};

struct HaUrl {
    bool     tls;
    char     host[96];
    uint16_t port;
    char     prefix[64];  // optional path prefix, no trailing slash
};

// Accepts "host", "host:port", "http(s)://host[:port][/prefix][/]".
bool ha_url_parse(const char *url, HaUrl &out);

// GET <ha_url>/api/display/ping (no credentials). Returns the HTTP status
// (200 = integration present), or -1 on network error.
int ha_check(const Config &cfg);

// Called for every state of a widget entity (snapshot and changes).
typedef void (*state_cb_t)(const char *entity_id, const char *state);

// Called for every layout frame: {widgets, settings{brightness, rotation}, screen, page, pager, rev, name}.
typedef void (*layout_cb_t)(JsonObjectConst frame);

void     ha_client_begin(const Config &cfg, state_cb_t on_state, layout_cb_t on_layout);
void     ha_client_stop();
void     ha_client_tick();
HaStatus ha_client_status();
const char *ha_client_error();

// Touch actions: the integration maps the widget id to the service call.
void ha_client_tap(const char *widget_id);
void ha_client_set(const char *widget_id, bool on);

// Page switch (dir > 0: next, dir < 0: previous). The integration answers with a new layout frame.
void ha_client_page(int dir);
