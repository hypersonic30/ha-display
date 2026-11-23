#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage.h"

enum HaStatus {
    HA_CONNECTING,      // socket not up / not yet answered
    HA_AUTH_FAILED,     // token rejected, client halted (no retries)
    HA_DEVICE_UNKNOWN,  // integration has no device with this name
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

// GET <ha_url>/api/ with the token. Returns the HTTP status, or -1 on network error.
int ha_check(const Config &cfg);

// Called for every state of a subscribed entity (initial snapshot and changes).
typedef void (*state_cb_t)(const char *entity_id, const char *state);

// Called for every layout event: {widgets, settings{brightness}, rev, name}.
typedef void (*layout_cb_t)(JsonObjectConst event);

void     ha_client_begin(const Config &cfg, state_cb_t on_state, layout_cb_t on_layout);
void     ha_client_stop();
void     ha_client_tick();
HaStatus ha_client_status();
const char *ha_client_error();

// Replaces the entity subscription (an empty list only cancels the old one).
void ha_client_subscribe(const char **entity_ids, size_t count);

void ha_client_call_service(const char *domain,
                            const char *service,
                            const char *entity_id);
