#include "ha_client.h"
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <WiFi.h>

static WebSocketsClient ws;
static state_cb_t  _on_state  = nullptr;
static layout_cb_t _on_layout = nullptr;
static Config      _cfg;

static bool _authenticated = false;
static bool _subscribed    = false;
static int  _msg_id        = 1;

// ── Message ID ───────────────────────────────────────────────────────────────

static int next_id() { return _msg_id++; }

// ── Send helpers ──────────────────────────────────────────────────────────────

static void ws_send(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

// ── Incoming message handling ─────────────────────────────────────────────────

static void handle_message(const char *payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    const char *type = doc["type"] | "";

    if (strcmp(type, "auth_required") == 0) {
        JsonDocument auth;
        auth["type"]         = "auth";
        auth["access_token"] = _cfg.ha_token;
        ws_send(auth);
        Serial.println("[HA] → auth");

    } else if (strcmp(type, "auth_ok") == 0) {
        _authenticated = true;
        Serial.println("[HA] authenticated");
        // After auth, fetch layout from integration
        // (done via HTTP in ha_client_tick on first run)

    } else if (strcmp(type, "auth_invalid") == 0) {
        Serial.println("[HA] auth INVALID — check token");

    } else if (strcmp(type, "event") == 0) {
        // State-changed event
        JsonObject event = doc["event"];
        if (!event) return;
        const char *ev_type = event["event_type"] | "";
        if (strcmp(ev_type, "state_changed") != 0) return;

        JsonObject data       = event["data"];
        JsonObject new_state  = data["new_state"];
        if (!new_state) return;

        const char *entity_id = new_state["entity_id"] | "";
        const char *state_val = new_state["state"]     | "";
        JsonObjectConst attrs = new_state["attributes"].as<JsonObjectConst>();

        if (_on_state) _on_state(entity_id, state_val, attrs);

    } else if (strcmp(type, "result") == 0) {
        // ACK for service calls etc. — ignore unless debugging
        if (!doc["success"].as<bool>()) {
            Serial.printf("[HA] result error: %s\n",
                          doc["error"]["message"] | "unknown");
        }
    }
}

// ── WebSocket event callback ──────────────────────────────────────────────────

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        Serial.println("[HA] WS connected");
        _authenticated = false;
        _subscribed    = false;
        break;
    case WStype_TEXT:
        handle_message((const char *)payload);
        break;
    case WStype_DISCONNECTED:
        _authenticated = false;
        _subscribed    = false;
        Serial.println("[HA] WS disconnected");
        break;
    case WStype_ERROR:
        Serial.println("[HA] WS error");
        break;
    default:
        break;
    }
}

// ── Layout fetch (HTTP) ───────────────────────────────────────────────────────

static bool _layout_fetched = false;

static void fetch_layout() {
    char url[256];
    // The HA integration exposes the layout at this endpoint
    snprintf(url, sizeof(url), "%s/api/display/layout/%s",
             _cfg.ha_url, _cfg.device_name);

    char auth[540];
    snprintf(auth, sizeof(auth), "Bearer %s", _cfg.ha_token);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", auth);
    int code = http.GET();

    if (code == 200) {
        String body = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            JsonArrayConst widgets = doc["widgets"].as<JsonArrayConst>();
            if (_on_layout) _on_layout(widgets);
            Serial.printf("[HA] layout loaded (%d widgets)\n", widgets.size());
        }
    } else if (code == 404) {
        Serial.println("[HA] no layout configured yet");
    } else {
        Serial.printf("[HA] layout fetch failed: %d\n", code);
    }
    http.end();
    _layout_fetched = true;
}

// ── Subscribe to state_changed for specific entities ─────────────────────────

void ha_client_subscribe(const char **entity_ids, size_t count) {
    if (!_authenticated) return;

    // Use subscribe_entities if available (HA ≥ 2022.4), filters server-side
    JsonDocument doc;
    doc["id"]   = next_id();
    doc["type"] = "subscribe_entities";
    JsonArray ids = doc["entity_ids"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) ids.add(entity_ids[i]);
    ws_send(doc);
    _subscribed = true;
    Serial.printf("[HA] subscribed to %u entities\n", count);
}

// ── Public API ────────────────────────────────────────────────────────────────

void ha_client_begin(const Config &cfg, state_cb_t on_state, layout_cb_t on_layout) {
    _cfg       = cfg;
    _on_state  = on_state;
    _on_layout = on_layout;

    // Parse host + port from ha_url (e.g. "http://192.168.1.10:8123")
    String url = String(cfg.ha_url);
    url.replace("http://", "");
    url.replace("https://", "");
    int colon = url.indexOf(':');
    String host = (colon > 0) ? url.substring(0, colon) : url;
    int    port  = (colon > 0) ? url.substring(colon + 1).toInt() : 8123;

    ws.begin(host.c_str(), port, "/api/websocket");
    ws.onEvent(ws_event);
    ws.setReconnectInterval(5000);
    Serial.printf("[HA] WS → %s:%d\n", host.c_str(), port);
}

void ha_client_tick() {
    ws.loop();

    // Fetch layout once after authentication
    if (_authenticated && !_layout_fetched) {
        fetch_layout();
    }
}

bool ha_client_connected() {
    return _authenticated;
}

void ha_client_call_service(const char *domain,
                            const char *service,
                            const char *entity_id) {
    if (!_authenticated) return;
    JsonDocument doc;
    doc["id"]      = next_id();
    doc["type"]    = "call_service";
    doc["domain"]  = domain;
    doc["service"] = service;
    doc["service_data"]["entity_id"] = entity_id;
    ws_send(doc);
    Serial.printf("[HA] call_service %s.%s(%s)\n", domain, service, entity_id);
}
