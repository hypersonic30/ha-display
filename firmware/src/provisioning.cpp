#include "provisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

static WebServer  server(80);
static DNSServer  dns;
static Config     saved_cfg;
static Config     cur_cfg;      // what is stored right now (read only, for the page)
static bool       cfg_ready = false;
static char       ap_pass[9];

static String trimmed(const char *s) {
    String t(s);
    t.trim();
    return t;
}

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
.hint{color:var(--muted);font-size:.8rem;margin-top:8px}
.net{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid #2a2a40}
.net:last-of-type{border-bottom:none}
.net span{overflow-wrap:anywhere}
.del{background:#4a1b1b;color:var(--err);border:none;border-radius:8px;padding:8px 12px;cursor:pointer;font-size:.85rem;white-space:nowrap}
.add-btn{width:100%;background:#333;border:none;border-radius:8px;color:var(--text);padding:12px;font-size:1rem;margin-top:12px;cursor:pointer}
.add-btn:disabled{opacity:.5;cursor:not-allowed}
.spinner{display:inline-block;width:16px;height:16px;border:2px solid #fff3;border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;vertical-align:middle;margin-right:8px}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<h1>ESP32 Display</h1>
<p class="sub">Einrichtung &middot; WLAN &amp; Home Assistant</p>

<form id="frm">
  <div class="card">
    <h2>Gespeicherte WLANs <span id="cnt"></span></h2>
    <div id="list"></div>
    <p class="hint">Das Display probiert sie der Reihe nach (zuerst das mit dem besten Empfang).</p>
  </div>

  <div class="card">
    <h2>WLAN hinzufügen</h2>
    <label>Netzwerkname (SSID)</label>
    <div class="row">
      <input id="ssid" list="nets" type="text" maxlength="32" placeholder="Name eintippen oder aus der Liste wählen" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false">
      <datalist id="nets"></datalist>
      <button type="button" class="scan-btn" onclick="scan()">&#8635;</button>
    </div>
    <p class="hint" id="scanhint">Die Suche zeigt nur Netze in Reichweite. Versteckte oder gerade ausgeschaltete Netze (z. B. ein Handy-Hotspot) einfach selbst eintippen.</p>
    <label>Passwort</label>
    <input id="pw" type="password" placeholder="WLAN-Passwort (leer bei offenem WLAN)" autocomplete="off">
    <button type="button" class="add-btn" id="add" onclick="addNet()">Hinzufügen</button>
    <p class="hint"><b>Handy-Hotspot:</b> Das Netz muss nicht jetzt erreichbar sein, es wird nur gespeichert. Beim iPhone schaltet sich der Hotspot ab, sobald du dich mit diesem Display-WLAN verbindest. Trag den Namen also von Hand ein, speichere, schalte danach den Hotspot wieder ein und lass dabei <i>Einstellungen &rarr; Persönlicher Hotspot</i> geöffnet. Aktiviere dort &bdquo;Kompatibilität maximieren&ldquo; (2,4 GHz), denn der ESP32 kann kein 5&nbsp;GHz. Das Display verbindet sich dann von selbst.</p>
  </div>

  <div class="card">
    <h2>Home Assistant</h2>
    <label>URL</label>
    <input name="ha_url" id="ha_url" type="text" placeholder="http://192.168.x.x:8123" required>
    <label>Gerätecode</label>
    <input name="device_key" id="device_key" type="password" placeholder="wird in Home Assistant unter 'Einrichtung' angezeigt" autocomplete="off" autocapitalize="off" spellcheck="false">
    <p class="hint" id="keyhint"></p>
  </div>

  <button type="submit" id="btn">Verbinden &amp; Speichern</button>
  <div class="status" id="st"></div>
</form>

<script>
const MAX=3;
let nets=[],hasKey=false;
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function status(msg,cls){const e=document.getElementById('st');e.className='status '+cls;e.innerHTML=msg}

function render(){
  document.getElementById('cnt').textContent='('+nets.length+' / '+MAX+')';
  document.getElementById('list').innerHTML=nets.length?nets.map((n,i)=>
    '<div class="net"><span>'+esc(n.ssid)+(n.keep?'':' <small>(neu)</small>')+'</span><button type="button" class="del" onclick="delNet('+i+')">Entfernen</button></div>').join('')
    :'<p class="hint">Noch kein WLAN gespeichert.</p>';
  document.getElementById('add').disabled=nets.length>=MAX&&!nets.some(n=>n.ssid===document.getElementById('ssid').value.trim());
}
function delNet(i){nets.splice(i,1);render()}
function addNet(){
  const box=document.getElementById('ssid');
  const ssid=box.value.trim();
  const pw=document.getElementById('pw').value;
  if(!ssid){status('Bitte einen Netzwerknamen eingeben','err');return}
  if(new TextEncoder().encode(ssid).length>32){status('Netzwerkname: höchstens 32 Bytes','err');return}
  if(pw&&pw.length<8){status('WLAN-Passwort: mindestens 8 Zeichen','err');return}
  const n={ssid:ssid,password:pw,keep:false};
  const i=nets.findIndex(x=>x.ssid===ssid);
  if(i>=0)nets[i]=n;else if(nets.length<MAX)nets.push(n);else return;
  document.getElementById('pw').value='';box.value='';
  status('','');document.getElementById('st').className='status';
  render();
}

async function scan(){
  const dl=document.getElementById('nets'),hint=document.getElementById('scanhint');
  hint.textContent='Suche läuft...';
  try{
    const r=await fetch('/scan');
    const found=await r.json();
    dl.innerHTML=found.map(n=>'<option value="'+esc(n.ssid)+'">'+esc(n.ssid)+' ('+n.rssi+' dBm)'+(n.open?' offen':'')+'</option>').join('');
    hint.textContent=found.length?found.length+' Netz(e) gefunden (ins Feld tippen zeigt die Liste). Nicht dabei? Einfach den Namen eintippen.':'Keine Netze gefunden. Du kannst den Namen trotzdem eintippen.';
  }catch(e){hint.textContent='Suche fehlgeschlagen. Du kannst den Namen trotzdem eintippen.'}
  render();
}

document.getElementById('ssid').addEventListener('input',render);

document.getElementById('frm').addEventListener('submit',async e=>{
  e.preventDefault();
  if(!nets.length){status('Mindestens ein WLAN speichern','err');return}
  const btn=document.getElementById('btn');
  btn.disabled=true;
  status('<span class="spinner"></span>Speichere und verbinde...','info');
  const data={
    nets:nets.map(n=>n.keep?{ssid:n.ssid,keep:true}:{ssid:n.ssid,password:n.password}),
    ha_url:document.getElementById('ha_url').value.trim().replace(/\/+$/,''),
    device_key:document.getElementById('device_key').value.trim()
  };
  try{
    const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    const j=await r.json();
    if(j.ok){
      status('Gespeichert. Das Display verbindet sich jetzt mit dem WLAN. Ist es ein Handy-Hotspot: jetzt wieder einschalten (Hotspot-Einstellungen geöffnet lassen), das Display verbindet sich von selbst.','ok');
    }else{
      status('Fehler: '+(j.error||'Unbekannt'),'err');
      btn.disabled=false;
    }
  }catch(e){
    status('Verbindungsfehler zur Firmware (evtl. schon gespeichert und verbunden)','err');
    btn.disabled=false;
  }
});

(async function(){
  try{
    const s=await (await fetch('/state')).json();
    nets=s.nets.map(x=>({ssid:x,keep:true}));
    hasKey=!!s.has_key;
    document.getElementById('ha_url').value=s.ha_url||'';
  }catch(e){}
  const q=new URLSearchParams(location.search);
  if(q.get('url'))document.getElementById('ha_url').value=q.get('url');
  if(q.get('key'))document.getElementById('device_key').value=q.get('key');
  const k=document.getElementById('device_key');
  if(hasKey&&!q.get('key')){k.required=false;document.getElementById('keyhint').textContent='Gespeichert. Leer lassen, um den Gerätecode zu behalten.'}
  else k.required=true;
  render();
  scan();
})();
</script>
</body>
</html>)html";

// ── State, scan & save handlers ───────────────────────────────────────────────

static void send_json(int code, const char *body) {
    server.send(code, "application/json", body);
}

// What the page shows as saved (no passwords, no device key).
static void handle_state() {
    JsonDocument doc;
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (int i = 0; i < cur_cfg.wifi_count; i++) nets.add((const char *)cur_cfg.wifi[i].ssid);
    doc["ha_url"]  = (const char *)cur_cfg.ha_url;
    doc["has_key"] = cur_cfg.device_key[0] != 0;
    String out;
    serializeJson(doc, out);
    send_json(200, out.c_str());
}

static void handle_scan() {
    int n = WiFi.scanNetworks();   // -1 while the station is in the middle of a connection attempt
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        if (WiFi.SSID(i).length() == 0) continue;   // hidden networks cannot be picked
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    send_json(200, out.c_str());
}

static void fail(const char *msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    String out;
    serializeJson(doc, out);
    send_json(400, out.c_str());
}

static void handle_save() {
    if (!server.hasArg("plain")) return fail("no body");
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) return fail("invalid json");

    String ha_url = trimmed(doc["ha_url"] | "");
    String key    = trimmed(doc["device_key"] | "");
    while (ha_url.endsWith("/")) ha_url.remove(ha_url.length() - 1);

    // A blank key keeps the saved one (the page never receives it).
    if (!key.length() && cur_cfg.device_key[0]) key = cur_cfg.device_key;

    JsonArrayConst in = doc["nets"].as<JsonArrayConst>();
    if (in.isNull() || in.size() == 0)   return fail("Mindestens ein WLAN speichern");
    if (in.size() > MAX_WIFI)            return fail("Höchstens 3 WLANs");
    if (!ha_url.length() || !key.length()) return fail("Felder fehlen");

    Config next;
    memset(&next, 0, sizeof(next));
    for (JsonObjectConst n : in) {
        const char *ssid = n["ssid"] | "";
        const char *pass = n["password"] | "";
        if (!ssid[0] || strlen(ssid) >= sizeof(next.wifi[0].ssid)) return fail("WLAN-Name ungültig");
        for (const char *c = ssid; *c; c++)
            if ((unsigned char)*c < 0x20 || *c == 0x7f) return fail("WLAN-Name ungültig");
        for (int i = 0; i < next.wifi_count; i++)
            if (strcmp(next.wifi[i].ssid, ssid) == 0) return fail("WLAN doppelt");
        WifiNet &w = next.wifi[next.wifi_count];
        strlcpy(w.ssid, ssid, sizeof(w.ssid));
        if (n["keep"] | false) {
            const WifiNet *old = nullptr;
            for (int i = 0; i < cur_cfg.wifi_count; i++)
                if (strcmp(cur_cfg.wifi[i].ssid, ssid) == 0) old = &cur_cfg.wifi[i];
            if (!old) return fail("Passwort für neues WLAN fehlt");
            strlcpy(w.pass, old->pass, sizeof(w.pass));
        } else {
            if (strlen(pass) >= sizeof(w.pass) || (pass[0] && strlen(pass) < 8)) return fail("WLAN-Passwort: 8 bis 63 Zeichen");
            strlcpy(w.pass, pass, sizeof(w.pass));
        }
        next.wifi_count++;
    }

    bool key_ok = key.length() < sizeof(next.device_key);
    for (size_t i = 0; key_ok && i < key.length(); i++) {
        char c = key[i];
        key_ok = isalnum((unsigned char)c) || c == '_' || c == '-';
    }
    if (!key_ok) return fail("Gerätecode ungültig (nur Buchstaben, Ziffern, _ und -)");
    if (!ha_url.startsWith("http://") && !ha_url.startsWith("https://"))
        return fail("URL muss mit http:// oder https:// beginnen");
    if (ha_url.length() >= sizeof(next.ha_url)) return fail("URL zu lang");

    strlcpy(next.ha_url,     ha_url.c_str(), sizeof(next.ha_url));
    strlcpy(next.device_key, key.c_str(),    sizeof(next.device_key));
    saved_cfg = next;
    cfg_ready = true;

    send_json(200, "{\"ok\":true}");
}

// ── Captive portal redirect ───────────────────────────────────────────────────

static void handle_captive() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

// ── Public API ────────────────────────────────────────────────────────────────

void provisioning_begin(const char *ap_ssid, const Config *current) {
    cfg_ready = false;
    memset(&cur_cfg, 0, sizeof(cur_cfg));
    if (current) cur_cfg = *current;

    // No look-alike characters (0/O, 1/I/L): the password is read off the display
    static const char CHARS[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 8; i++) ap_pass[i] = CHARS[esp_random() % (sizeof(CHARS) - 1)];
    ap_pass[8] = 0;

    WiFi.mode(WIFI_AP_STA);  // AP + STA so we can scan networks (and keep a station connection running)
    WiFi.softAP(ap_ssid, ap_pass);
    delay(100);

    dns.start(53, "*", IPAddress(192, 168, 4, 1));

    static bool routes = false;   // handlers are registered once, the server is just restarted
    if (!routes) {
        routes = true;
        server.on("/",        HTTP_GET,  []() { server.send_P(200, "text/html", HTML); });
        server.on("/state",   HTTP_GET,  handle_state);
        server.on("/scan",    HTTP_GET,  handle_scan);
        server.on("/save",    HTTP_POST, handle_save);
        // Captive portal endpoints (Android, Windows, iOS)
        server.on("/generate_204",              handle_captive);
        server.on("/connecttest.txt",           handle_captive);
        server.on("/redirect",                  handle_captive);
        server.on("/hotspot-detect.html",       handle_captive);
        server.on("/library/test/success.html", handle_captive);
        server.onNotFound([]() { server.send_P(200, "text/html", HTML); });
    }

    server.begin();
    Serial.println("[PROV] AP started, web server on 192.168.4.1");
}

const char *provisioning_ap_pass() {
    return ap_pass;
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
