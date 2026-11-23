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

static bool     _connected   = false;
static bool     _have_layout = false;
static bool     _halted      = true;
static HaStatus _status      = HA_CONNECTING;
static char     _error[96]   = "";

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
    snprintf(url, sizeof(url), "%s://%s:%u%s/api/display/ping", u.tls ? "https" : "http",
             u.host, (unsigned)u.port, u.prefix);

    WiFiClient       plain;
    WiFiClientSecure secure;
    secure.setInsecure();  // LAN device without a CA store: encrypts, does not authenticate

    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(5000);
    bool ok = u.tls ? http.begin(secure, url) : http.begin(plain, url);
    if (!ok) return -1;
    int code = http.GET();
    http.end();
    Serial.printf("[HA] GET %s -> %d\n", url, code);
    return code;
}

// ── Send helpers ──────────────────────────────────────────────────────────────

static void ws_send(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

static void send_hello() {
    JsonDocument doc;
    doc["type"] = "hello";
    doc["key"]  = _cfg.device_key;
    doc["fw"]   = FW_VERSION;
    ws_send(doc);
    Serial.println("[HA] hello");
}

// ── Incoming frames ───────────────────────────────────────────────────────────

static void handle_message(const char *payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char *type = doc["type"] | "";

    if (strcmp(type, "layout") == 0) {
        _have_layout = true;
        _status = HA_OK;
        if (_on_layout) _on_layout(doc.as<JsonObjectConst>());

    } else if (strcmp(type, "states") == 0) {
        if (!_on_state) return;
        for (JsonPairConst kv : doc["states"].as<JsonObjectConst>()) {
            const char *s = kv.value().as<const char *>();
            if (s) _on_state(kv.key().c_str(), s);
        }

    } else if (strcmp(type, "error") == 0) {
        const char *code = doc["code"] | "";
        if (strcmp(code, "auth") == 0) {
            // Never retry: a wrong code must not hammer the integration.
            Serial.println("[HA] device code rejected, halting");
            _status = HA_AUTH_FAILED;
            _halted = true;
            ws.disconnect();
        } else {
            _status = HA_PROTOCOL_ERROR;
            strlcpy(_error, doc["message"] | code, sizeof(_error));
            Serial.printf("[HA] error: %s\n", _error);
        }
    }
}

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        Serial.println("[HA] WS connected");
        _connected   = true;
        _have_layout = false;
        send_hello();
        break;
    case WStype_TEXT:
        handle_message((const char *)payload, length);
        break;
    case WStype_DISCONNECTED:
        Serial.println("[HA] WS disconnected");
        _connected   = false;
        _have_layout = false;
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

    HaUrl u;
    ha_url_parse(cfg.ha_url, u);
    char path[96];
    snprintf(path, sizeof(path), "%s/api/display/ws", u.prefix);

    if (u.tls) ws.beginSSL(u.host, u.port, path);
    else       ws.begin(u.host, u.port, path);
    ws.onEvent(ws_event);
    ws.setReconnectInterval(5000);
    ws.enableHeartbeat(15000, 3000, 2);

    _halted = false;
    _status = HA_CONNECTING;
    _connected = _have_layout = false;
    Serial.printf("[HA] WS -> %s://%s:%u%s\n", u.tls ? "wss" : "ws", u.host,
                  (unsigned)u.port, path);
}

void ha_client_stop() {
    if (!_halted) ws.disconnect();
    _halted = true;
    _connected = _have_layout = false;
}

void ha_client_tick() {
    if (_halted) return;
    ws.loop();
}

HaStatus ha_client_status() { return _status; }
const char *ha_client_error() { return _error; }

void ha_client_tap(const char *widget_id) {
    if (!_connected || !_have_layout || !widget_id || !widget_id[0]) return;
    JsonDocument doc;
    doc["type"]   = "tap";
    doc["widget"] = widget_id;
    ws_send(doc);
    Serial.printf("[HA] tap %s\n", widget_id);
}

void ha_client_set(const char *widget_id, bool on) {
    if (!_connected || !_have_layout || !widget_id || !widget_id[0]) return;
    JsonDocument doc;
    doc["type"]   = "set";
    doc["widget"] = widget_id;
    doc["on"]     = on;
    ws_send(doc);
    Serial.printf("[HA] set %s %d\n", widget_id, on);
}

void ha_client_page(int dir) {
    if (!_connected || !_have_layout || dir == 0) return;
    JsonDocument doc;
    doc["type"] = "page";
    doc["dir"]  = dir > 0 ? "next" : "prev";
    ws_send(doc);
    Serial.printf("[HA] page %s\n", dir > 0 ? "next" : "prev");
}
