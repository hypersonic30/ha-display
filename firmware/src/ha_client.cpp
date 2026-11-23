#include "ha_client.h"
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <strings.h>

static WebSocketsClient ws;
static state_cb_t  _on_state  = nullptr;
static layout_cb_t _on_layout = nullptr;
static Config      _cfg;

static bool     _authenticated   = false;
static bool     _halted          = true;
static HaStatus _status          = HA_CONNECTING;
static char     _error[96]       = "";
static int      _msg_id          = 1;
static int      _layout_sub_id   = 0;
static int      _entity_sub_id   = 0;
static uint32_t _layout_try      = 0;

static int next_id() { return _msg_id++; }

// ── URL handling ──────────────────────────────────────────────────────────────

bool ha_url_parse(const char *url, HaUrl &out) {
    memset(&out, 0, sizeof(out));
    while (*url == ' ' || *url == '\t') url++;

    char buf[160];
    strlcpy(buf, url, sizeof(buf));
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == ' ' || buf[n - 1] == '\t' || buf[n - 1] == '/')) buf[--n] = 0;

    char *p = buf;
    if (strncasecmp(p, "https://", 8) == 0)      { out.tls = true; p += 8; }
    else if (strncasecmp(p, "http://", 7) == 0)  { p += 7; }

    char *slash = strchr(p, '/');
    if (slash) {
        strlcpy(out.prefix, slash, sizeof(out.prefix));
        *slash = 0;
    }
    char *colon = strrchr(p, ':');
    out.port = out.tls ? 443 : 8123;
    if (colon) {
        *colon = 0;
        int port = atoi(colon + 1);
        if (port > 0 && port < 65536) out.port = (uint16_t)port;
    }
    strlcpy(out.host, p, sizeof(out.host));
    return out.host[0] != 0;
}

int ha_check(const Config &cfg) {
    HaUrl u;
    if (!ha_url_parse(cfg.ha_url, u)) return -1;

    char url[256];
    snprintf(url, sizeof(url), "%s://%s:%u%s/api/", u.tls ? "https" : "http",
             u.host, (unsigned)u.port, u.prefix);
    char auth[540];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.ha_token);

    WiFiClient       plain;
    WiFiClientSecure secure;
    secure.setInsecure();  // LAN device without a CA store: encrypts, does not authenticate

    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(5000);
    bool ok = u.tls ? http.begin(secure, url) : http.begin(plain, url);
    if (!ok) return -1;
    http.addHeader("Authorization", auth);
    int code = http.GET();
    http.end();
    Serial.printf("[HA] GET %s -> %d\n", url, code);
    return code;
}

// ── JSON filter: drop everything the firmware does not use (attributes are huge) ──

static JsonDocument _filter;

static void init_filter() {
    _filter["id"]      = true;
    _filter["type"]    = true;
    _filter["success"] = true;
    _filter["error"]["code"]    = true;
    _filter["error"]["message"] = true;
    _filter["event"]["widgets"]  = true;
    _filter["event"]["settings"] = true;
    _filter["event"]["rev"]      = true;
    _filter["event"]["name"]     = true;
    _filter["event"]["a"]["*"]["s"]        = true;
    _filter["event"]["c"]["*"]["+"]["s"]   = true;
    _filter["event"]["r"]                  = true;
}

// ── Send helpers ──────────────────────────────────────────────────────────────

static void ws_send(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

static void send_layout_subscribe() {
    _layout_try    = millis();
    _layout_sub_id = next_id();
    JsonDocument doc;
    doc["id"]     = _layout_sub_id;
    doc["type"]   = "display/layout/subscribe";
    doc["device"] = _cfg.device_name;
    ws_send(doc);
    Serial.printf("[HA] layout subscribe '%s'\n", _cfg.device_name);
}

// ── Incoming messages ─────────────────────────────────────────────────────────

// Format of subscribe_entities (HA websocket_api/messages.py):
//   {"a": {eid: {"s": state, ...}}}          initial snapshot / new entity
//   {"c": {eid: {"+": {"s": state, ...}}}}   diff, "s" only if the state changed
//   {"r": [eid, ...]}                        entity removed
static void handle_entity_event(JsonObjectConst ev) {
    if (!_on_state) return;
    for (JsonPairConst kv : ev["a"].as<JsonObjectConst>()) {
        const char *s = kv.value()["s"];
        if (s) _on_state(kv.key().c_str(), s);
    }
    for (JsonPairConst kv : ev["c"].as<JsonObjectConst>()) {
        const char *s = kv.value()["+"]["s"];
        if (s) _on_state(kv.key().c_str(), s);
    }
    for (JsonVariantConst r : ev["r"].as<JsonArrayConst>()) {
        const char *eid = r.as<const char *>();
        if (eid) _on_state(eid, "unavailable");
    }
}

static void handle_result(JsonDocument &doc, int id) {
    if (doc["success"].as<bool>()) return;
    const char *code = doc["error"]["code"]    | "";
    const char *msg  = doc["error"]["message"] | "unknown";
    Serial.printf("[HA] error (id %d): %s %s\n", id, code, msg);
    if (id != _layout_sub_id) return;  // failed service calls etc. are only logged

    if (strcmp(code, "not_found") == 0) {
        _status = HA_DEVICE_UNKNOWN;
    } else {
        _status = HA_PROTOCOL_ERROR;
        strlcpy(_error, strcmp(code, "unknown_command") == 0
                            ? "Integration 'display' fehlt oder ist veraltet"
                            : msg, sizeof(_error));
    }
}

static void handle_message(const char *payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length,
                        DeserializationOption::Filter(_filter)) != DeserializationError::Ok) return;

    const char *type = doc["type"] | "";
    int id = doc["id"] | 0;

    if (strcmp(type, "auth_required") == 0) {
        JsonDocument auth;
        auth["type"]         = "auth";
        auth["access_token"] = _cfg.ha_token;
        ws_send(auth);

    } else if (strcmp(type, "auth_ok") == 0) {
        _authenticated = true;
        Serial.println("[HA] authenticated");
        send_layout_subscribe();

    } else if (strcmp(type, "auth_invalid") == 0) {
        // Never retry: repeated failures get the IP banned by HA.
        Serial.println("[HA] auth INVALID, halting");
        _status = HA_AUTH_FAILED;
        _halted = true;
        ws.disconnect();

    } else if (strcmp(type, "event") == 0) {
        JsonObjectConst ev = doc["event"].as<JsonObjectConst>();
        if (id && id == _layout_sub_id) {
            _status = HA_OK;
            if (_on_layout) _on_layout(ev);
        } else if (id && id == _entity_sub_id) {
            handle_entity_event(ev);
        }

    } else if (strcmp(type, "result") == 0) {
        handle_result(doc, id);
    }
}

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        Serial.println("[HA] WS connected");
        _authenticated = false;
        _msg_id = 1;
        _layout_sub_id = _entity_sub_id = 0;
        break;
    case WStype_TEXT:
        handle_message((const char *)payload, length);
        break;
    case WStype_DISCONNECTED:
        Serial.println("[HA] WS disconnected");
        _authenticated = false;
        _layout_sub_id = _entity_sub_id = 0;
        if (!_halted) _status = HA_CONNECTING;
        break;
    default:
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ha_client_begin(const Config &cfg, state_cb_t on_state, layout_cb_t on_layout) {
    _cfg       = cfg;
    _on_state  = on_state;
    _on_layout = on_layout;
    init_filter();

    HaUrl u;
    ha_url_parse(cfg.ha_url, u);
    char path[96];
    snprintf(path, sizeof(path), "%s/api/websocket", u.prefix);

    if (u.tls) ws.beginSSL(u.host, u.port, path);
    else       ws.begin(u.host, u.port, path);
    ws.onEvent(ws_event);
    ws.setReconnectInterval(5000);
    ws.enableHeartbeat(15000, 3000, 2);

    _halted = false;
    _status = HA_CONNECTING;
    _authenticated = false;
    _layout_sub_id = _entity_sub_id = 0;
    Serial.printf("[HA] WS -> %s://%s:%u%s\n", u.tls ? "wss" : "ws", u.host,
                  (unsigned)u.port, path);
}

void ha_client_stop() {
    if (!_halted) ws.disconnect();
    _halted = true;
    _authenticated = false;
}

void ha_client_tick() {
    if (_halted) return;
    ws.loop();

    // The device may be added in HA later: ask again, this is no auth attempt.
    if (_authenticated && (_status == HA_DEVICE_UNKNOWN || _status == HA_PROTOCOL_ERROR) &&
        millis() - _layout_try > 15000) {
        send_layout_subscribe();
    }
}

HaStatus ha_client_status() { return _status; }
const char *ha_client_error() { return _error; }

void ha_client_subscribe(const char **entity_ids, size_t count) {
    if (!_authenticated) return;

    if (_entity_sub_id) {
        JsonDocument un;
        un["id"]           = next_id();
        un["type"]         = "unsubscribe_events";
        un["subscription"] = _entity_sub_id;
        ws_send(un);
        _entity_sub_id = 0;
    }
    // An empty entity_ids list would subscribe to ALL entities in HA.
    if (count == 0) return;

    _entity_sub_id = next_id();
    JsonDocument doc;
    doc["id"]   = _entity_sub_id;
    doc["type"] = "subscribe_entities";
    JsonArray ids = doc["entity_ids"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) ids.add(entity_ids[i]);
    ws_send(doc);
    Serial.printf("[HA] subscribed to %u entities\n", (unsigned)count);
}

void ha_client_call_service(const char *domain, const char *service,
                            const char *entity_id) {
    if (!_authenticated || !entity_id || !entity_id[0]) return;
    JsonDocument doc;
    doc["id"]      = next_id();
    doc["type"]    = "call_service";
    doc["domain"]  = domain;
    doc["service"] = service;
    doc["service_data"]["entity_id"] = entity_id;
    ws_send(doc);
    Serial.printf("[HA] call_service %s.%s(%s)\n", domain, service, entity_id);
}
