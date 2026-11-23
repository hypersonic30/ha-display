#include "provisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

static WebServer  server(80);
static DNSServer  dns;
static Config     saved_cfg;
static bool       cfg_ready = false;

// ── HTML page (embedded) ──────────────────────────────────────────────────────

static const char HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Display · Setup</title>
<style>
:root{--bg:#0f0f1a;--card:#1a1a2e;--accent:#4fc3f7;--text:#e0e0e0;--muted:#888;--err:#ef5350;--ok:#66bb6a}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif;padding:20px;min-height:100vh}
h1{color:var(--accent);font-size:1.4rem;margin-bottom:4px}
.sub{color:var(--muted);font-size:.85rem;margin-bottom:24px}
.card{background:var(--card);border-radius:12px;padding:20px;margin-bottom:16px}
.card h2{font-size:1rem;color:var(--accent);margin-bottom:16px}
label{display:block;font-size:.85rem;color:var(--muted);margin-bottom:4px;margin-top:12px}
label:first-of-type{margin-top:0}
input,select{width:100%;background:#0f0f1a;border:1px solid #333;border-radius:8px;color:var(--text);padding:10px 12px;font-size:1rem}
input:focus,select:focus{outline:none;border-color:var(--accent)}
.row{display:flex;gap:8px;align-items:flex-end}
.row select{flex:1}
.scan-btn{background:#333;border:none;border-radius:8px;color:var(--text);padding:10px 14px;cursor:pointer;white-space:nowrap;font-size:1rem}
.scan-btn:active{background:#444}
button[type=submit]{width:100%;background:var(--accent);color:#000;border:none;border-radius:8px;padding:14px;font-size:1rem;font-weight:600;cursor:pointer;margin-top:8px}
button[type=submit]:disabled{opacity:.5;cursor:not-allowed}
.status{text-align:center;padding:12px;border-radius:8px;margin-top:12px;display:none}
.status.ok{background:#1b4a1f;color:var(--ok);display:block}
.status.err{background:#4a1b1b;color:var(--err);display:block}
.status.info{background:#1a2a3a;color:var(--accent);display:block}
.spinner{display:inline-block;width:16px;height:16px;border:2px solid #fff3;border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;vertical-align:middle;margin-right:8px}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<h1>ESP32 Display</h1>
<p class="sub">Ersteinrichtung</p>

<form id="frm">
  <div class="card">
    <h2>WLAN</h2>
    <label>Netzwerk</label>
    <div class="row">
      <select id="ssid" name="ssid" required><option value="">Suche läuft...</option></select>
      <button type="button" class="scan-btn" onclick="scan()">&#8635;</button>
    </div>
    <label>Passwort</label>
    <input name="password" type="password" placeholder="WLAN-Passwort" autocomplete="off">
  </div>

  <div class="card">
    <h2>Home Assistant</h2>
    <label>URL</label>
    <input name="ha_url" type="text" placeholder="http://192.168.x.x:8123" required>
    <label>Long-Lived Access Token</label>
    <input name="ha_token" type="password" placeholder="eyJ..." required autocomplete="off">
    <label>Gerätename</label>
    <input name="device_name" placeholder="z.B. Küche" required value="ESP32 Display">
  </div>

  <button type="submit" id="btn">Verbinden &amp; Speichern</button>
  <div class="status" id="st"></div>
</form>

<script>
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function status(msg,cls){const e=document.getElementById('st');e.className='status '+cls;e.innerHTML=msg}

async function scan(){
  const sel=document.getElementById('ssid');
  sel.innerHTML='<option>Suche...</option>';
  try{
    const r=await fetch('/scan');
    const nets=await r.json();
    if(!nets.length){sel.innerHTML='<option value="">Keine Netzwerke gefunden</option>';return}
    sel.innerHTML=nets.map(n=>`<option value="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm)${n.open?' 🔓':''}</option>`).join('');
  }catch(e){sel.innerHTML='<option value="">Fehler beim Suchen</option>'}
}

document.getElementById('frm').addEventListener('submit',async e=>{
  e.preventDefault();
  const btn=document.getElementById('btn');
  btn.disabled=true;
  status('<span class="spinner"></span>Speichere und starte neu...','info');
  const data=Object.fromEntries(new FormData(e.target));
  try{
    const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    const j=await r.json();
    if(j.ok){
      status('Gespeichert! Das Display verbindet sich und startet neu.','ok');
    }else{
      status('Fehler: '+(j.error||'Unbekannt'),'err');
      btn.disabled=false;
    }
  }catch(e){
    status('Verbindungsfehler zur Firmware','err');
    btn.disabled=false;
  }
});

scan();
</script>
</body>
</html>)html";

// ── Scan handler ──────────────────────────────────────────────────────────────

static void handle_scan() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ── Save handler ──────────────────────────────────────────────────────────────

static void handle_save() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
        return;
    }

    const char *ssid   = doc["ssid"]        | "";
    const char *pass   = doc["password"]    | "";
    const char *ha_url = doc["ha_url"]      | "";
    const char *token  = doc["ha_token"]    | "";
    const char *name   = doc["device_name"] | "ESP32 Display";

    if (!ssid[0] || !ha_url[0] || !token[0]) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Felder fehlen\"}");
        return;
    }

    strlcpy(saved_cfg.wifi_ssid,   ssid,   sizeof(saved_cfg.wifi_ssid));
    strlcpy(saved_cfg.wifi_pass,   pass,   sizeof(saved_cfg.wifi_pass));
    strlcpy(saved_cfg.ha_url,      ha_url, sizeof(saved_cfg.ha_url));
    strlcpy(saved_cfg.ha_token,    token,  sizeof(saved_cfg.ha_token));
    strlcpy(saved_cfg.device_name, name,   sizeof(saved_cfg.device_name));
    cfg_ready = true;

    server.send(200, "application/json", "{\"ok\":true}");
}

// ── Captive portal redirect ───────────────────────────────────────────────────

static void handle_captive() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

// ── Public API ────────────────────────────────────────────────────────────────

void provisioning_begin(const char *ap_ssid) {
    cfg_ready = false;

    WiFi.mode(WIFI_AP_STA);  // AP + STA so we can scan networks
    WiFi.softAP(ap_ssid);
    delay(100);

    dns.start(53, "*", IPAddress(192, 168, 4, 1));

    server.on("/",        HTTP_GET,  []() { server.send_P(200, "text/html", HTML); });
    server.on("/scan",    HTTP_GET,  handle_scan);
    server.on("/save",    HTTP_POST, handle_save);
    // Captive portal endpoints (Android, Windows, iOS)
    server.on("/generate_204",              handle_captive);
    server.on("/connecttest.txt",           handle_captive);
    server.on("/redirect",                  handle_captive);
    server.on("/hotspot-detect.html",       handle_captive);
    server.on("/library/test/success.html", handle_captive);
    server.onNotFound([]() { server.send_P(200, "text/html", HTML); });

    server.begin();
    Serial.println("[PROV] AP started, web server on 192.168.4.1");
}

void provisioning_handle() {
    dns.processNextRequest();
    server.handleClient();
}

bool provisioning_done() {
    return cfg_ready;
}

Config provisioning_config() {
    return saved_cfg;
}

void provisioning_end() {
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("[PROV] AP stopped");
}
