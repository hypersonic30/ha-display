// ESP32 Display card: visual layout editor for the panels set up in the "ESP32 Display" integration.
// Talks to Home Assistant only through the integration's WebSocket commands (display/...).

const CARD_VERSION = "0.2.1";
const SCREEN_W = 480, SCREEN_H = 320, GRID = 5, MAX_WIDGETS = 32, MIN_SIZE = 20;

const TYPES = {
  label:  { icon: "Aa", name: "Wert",     w: 180, h: 70,  domains: null },
  button: { icon: "▣",  name: "Taster",   w: 130, h: 80,  domains: ["light", "switch", "input_boolean", "automation", "script", "scene", "button", "input_button", "fan", "cover"] },
  gauge:  { icon: "◔",  name: "Anzeige",  w: 150, h: 150, domains: ["sensor", "input_number", "number"] },
  switch: { icon: "⇆",  name: "Schalter", w: 170, h: 60,  domains: ["light", "switch", "input_boolean", "fan", "automation"] },
  bar:    { icon: "▬",  name: "Balken",   w: 210, h: 60,  domains: ["sensor", "input_number", "number"] },
};

const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const snap = (v) => Math.round(v / GRID) * GRID;
const num = (v, fallback) => (Number.isFinite(parseFloat(v)) ? parseFloat(v) : fallback);

const STYLE = `
:host { display: block; }
* { box-sizing: border-box; }
.root { background: var(--card-background-color); border-radius: var(--ha-card-border-radius, 12px); overflow: hidden; color: var(--primary-text-color); font-family: var(--paper-font-body1_-_font-family, sans-serif); }
.bar { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 10px 14px; border-bottom: 1px solid var(--divider-color); }
.bar select, .field input, .field select { background: var(--secondary-background-color); color: var(--primary-text-color); border: 1px solid var(--divider-color); border-radius: 8px; padding: 7px 9px; font-size: 14px; min-width: 0; }
.bar select { flex: 1 1 140px; }
.dot { width: 10px; height: 10px; border-radius: 50%; background: var(--disabled-text-color); flex: none; }
.dot.on { background: var(--success-color, #43a047); }
.btn { border: 0; border-radius: 8px; padding: 8px 14px; font-size: 13px; font-weight: 500; cursor: pointer; background: var(--secondary-background-color); color: var(--primary-text-color); }
.btn.primary { background: var(--primary-color); color: var(--text-primary-color, #fff); }
.btn.danger { background: var(--error-color, #db4437); color: #fff; }
.btn:disabled { opacity: .45; cursor: default; }
.setup { padding: 12px 14px; border-bottom: 1px solid var(--divider-color); background: var(--secondary-background-color); font-size: 13px; }
.setup .row { display: flex; gap: 6px; align-items: center; margin-top: 6px; }
.setup input { flex: 1; min-width: 0; font-family: monospace; }
.setup .hint { color: var(--secondary-text-color); margin: 0 0 4px; }
.body { display: flex; flex-wrap: wrap; }
.palette { display: flex; flex-direction: column; gap: 6px; padding: 12px 10px; width: 104px; border-right: 1px solid var(--divider-color); }
.pi { text-align: center; padding: 8px 2px; border: 1px solid var(--divider-color); border-radius: 8px; cursor: grab; user-select: none; background: var(--secondary-background-color); font-size: 12px; }
.pi b { display: block; font-size: 18px; font-weight: 500; }
.stage { flex: 1 1 360px; min-width: 0; padding: 14px; background: var(--secondary-background-color); display: flex; justify-content: center; align-items: flex-start; }
.screen { position: relative; flex: none; background: #0f0f1a; border-radius: 6px; box-shadow: 0 3px 16px rgba(0,0,0,.45); overflow: hidden; outline: none; touch-action: none; }
.screen:focus-visible { box-shadow: 0 0 0 2px var(--primary-color); }
.canvas { position: absolute; left: 0; top: 0; width: ${SCREEN_W}px; height: ${SCREEN_H}px; transform-origin: 0 0;
  background-image: radial-gradient(#ffffff14 1px, transparent 1px); background-size: ${GRID * 4}px ${GRID * 4}px; }
.w { position: absolute; background: #1a1a2e; border-radius: 10px; border: 2px solid transparent; padding: 8px; overflow: hidden; cursor: grab; touch-action: none; user-select: none; color: #e0e0e0; font-family: sans-serif; }
.w.sel { border-color: #4fc3f7; }
.w .t { font-size: 12px; color: #777799; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; pointer-events: none; }
.w .v { font-size: 20px; position: absolute; left: 8px; bottom: 6px; right: 8px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; pointer-events: none; }
.w.button { background: #2a2a44; text-align: center; } .w.button.on { background: #4fc3f7; color: #0f0f1a; }
.w.button .v { position: static; text-align: center; font-size: 16px; display: flex; align-items: center; justify-content: center; height: 100%; }
.w.gauge .t { text-align: center; } .w.gauge .v { text-align: center; bottom: auto; top: 50%; transform: translateY(-40%); }
.ring { position: absolute; left: 50%; top: 54%; transform: translate(-50%, -50%); border-radius: 50%; pointer-events: none; }
.ring::after { content: ""; position: absolute; inset: 7px; border-radius: 50%; background: #1a1a2e; }
.w.switch .t { position: absolute; left: 8px; top: 50%; transform: translateY(-50%); max-width: 55%; }
.pill { position: absolute; right: 8px; top: 50%; transform: translateY(-50%); width: 50px; height: 25px; border-radius: 13px; background: #2a2a44; pointer-events: none; }
.pill::after { content: ""; position: absolute; top: 3px; left: 3px; width: 19px; height: 19px; border-radius: 50%; background: #777799; transition: left .15s; }
.pill.on { background: #4fc3f7; } .pill.on::after { left: 28px; background: #fff; }
.w.bar .v { top: 6px; bottom: auto; left: auto; font-size: 14px; color: #777799; }
.track { position: absolute; left: 8px; right: 8px; bottom: 8px; height: 12px; border-radius: 6px; background: #2a2a44; pointer-events: none; }
.track i { display: block; height: 100%; border-radius: 6px; background: #4fc3f7; }
.rz { position: absolute; right: 0; bottom: 0; width: 18px; height: 18px; cursor: nwse-resize; touch-action: none; }
.rz::after { content: ""; position: absolute; right: 3px; bottom: 3px; width: 8px; height: 8px; border-right: 2px solid #4fc3f7; border-bottom: 2px solid #4fc3f7; }
.props { flex: 0 0 230px; padding: 14px; border-left: 1px solid var(--divider-color); }
.props h3 { margin: 0 0 10px; font-size: 14px; }
.empty { color: var(--secondary-text-color); font-size: 13px; line-height: 1.5; }
.field { margin-bottom: 9px; } .field label { display: block; font-size: 11px; color: var(--secondary-text-color); margin-bottom: 3px; } .field input, .field select { width: 100%; }
.two { display: flex; gap: 6px; } .two .field { flex: 1; }
.msg { margin: 10px 14px; padding: 8px 10px; border-radius: 8px; font-size: 13px; background: var(--secondary-background-color); }
.msg.err { background: var(--error-color, #db4437); color: #fff; }
@media (max-width: 700px) { .palette { flex-direction: row; width: 100%; border-right: 0; border-bottom: 1px solid var(--divider-color); overflow-x: auto; } .pi { flex: 1 0 70px; } .props { flex: 1 1 100%; border-left: 0; border-top: 1px solid var(--divider-color); } }
`;

class DisplayCard extends HTMLElement {
  static getConfigElement() { return document.createElement("display-card-editor"); }
  static getStubConfig() { return {}; }

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._config = {};
    this._devices = [];
    this._deviceId = null;
    this._widgets = [];
    this._selected = null;
    this._dirty = false;
    this._scale = 1;
    this._sig = "";
  }

  setConfig(config) { this._config = config || {}; }
  getCardSize() { return 8; }

  set hass(hass) {
    const first = !this._hass;
    this._hass = hass;
    if (first) this._init();
    else this._refreshValues();
  }

  // ── Startup ───────────────────────────────────────────────────────────────

  async _init() {
    this.shadowRoot.innerHTML = `<style>${STYLE}</style><div class="root"><div class="msg">Lade …</div></div>`;
    try {
      this._devices = (await this._hass.callWS({ type: "display/devices" })).devices;
    } catch (err) {
      return this._fatal("Die Integration „ESP32 Display“ ist nicht eingerichtet oder nicht erreichbar.");
    }
    if (!this._devices.length) {
      return this._fatal("Noch kein Display angelegt: Einstellungen → Geräte & Dienste → ESP32 Display → „Display hinzufügen“.");
    }
    const wanted = String(this._config.device || "").trim().toLowerCase();
    const match = this._devices.find((d) => d.name.toLowerCase() === wanted || d.id === this._config.device);
    this._deviceId = (match || this._devices[0]).id;
    this._renderShell();
    await this._load();
  }

  _fatal(text) {
    this.shadowRoot.innerHTML = `<style>${STYLE}</style><div class="root"><div class="msg err">${esc(text)}</div></div>`;
  }

  _renderShell() {
    const fixed = !!this._config.device && this._devices.some((d) => d.name.toLowerCase() === String(this._config.device).toLowerCase());
    this.shadowRoot.innerHTML = `<style>${STYLE}</style>
      <div class="root">
        <div class="bar">
          <span class="dot" id="dot" title="Display verbunden?"></span>
          <select id="dev" ${fixed ? "disabled" : ""}>${this._devices.map((d) => `<option value="${esc(d.id)}">${esc(d.name)}</option>`).join("")}</select>
          <button class="btn" id="setup">Einrichtung</button>
          <button class="btn" id="reload" title="Gespeicherten Stand neu laden">↻</button>
          <button class="btn primary" id="save" disabled>Speichern</button>
        </div>
        <div id="setupbox"></div>
        <div id="msg"></div>
        <div class="body">
          <div class="palette">${Object.entries(TYPES).map(([t, d]) => `<div class="pi" draggable="true" data-type="${t}" title="Ziehen oder anklicken"><b>${d.icon}</b>${d.name}</div>`).join("")}</div>
          <div class="stage"><div class="screen" id="screen" tabindex="0"><div class="canvas" id="canvas"></div></div></div>
          <div class="props" id="props"></div>
        </div>
      </div>`;
    const $ = (id) => this.shadowRoot.getElementById(id);
    $("dev").value = this._deviceId;
    $("dev").addEventListener("change", (e) => this._switchDevice(e.target.value));
    $("save").addEventListener("click", () => this._save());
    $("reload").addEventListener("click", () => this._reload());
    $("setup").addEventListener("click", () => this._toggleSetup());
    this.shadowRoot.querySelectorAll(".pi").forEach((el) => {
      el.addEventListener("dragstart", (e) => e.dataTransfer.setData("text/plain", el.dataset.type));
      el.addEventListener("click", () => this._add(el.dataset.type, 20, 20));
    });
    const screen = $("screen");
    screen.addEventListener("dragover", (e) => e.preventDefault());
    screen.addEventListener("drop", (e) => {
      e.preventDefault();
      const type = e.dataTransfer.getData("text/plain");
      if (!TYPES[type]) return;
      const r = screen.getBoundingClientRect();
      this._add(type, (e.clientX - r.left) / this._scale - TYPES[type].w / 2, (e.clientY - r.top) / this._scale - TYPES[type].h / 2);
    });
    screen.addEventListener("pointerdown", (e) => { if (e.target === screen || e.target.id === "canvas") this._select(null); });
    screen.addEventListener("keydown", (e) => this._key(e));
    const stage = this.shadowRoot.querySelector(".stage");
    new ResizeObserver(() => this._fit(stage)).observe(stage);
    this._renderProps();
  }

  _fit(stage) {
    const avail = stage.clientWidth - 28;
    if (avail <= 0) return;
    this._scale = Math.min(1.6, avail / SCREEN_W);
    const screen = this.shadowRoot.getElementById("screen");
    screen.style.width = `${SCREEN_W * this._scale}px`;
    screen.style.height = `${SCREEN_H * this._scale}px`;
    this.shadowRoot.getElementById("canvas").style.transform = `scale(${this._scale})`;
  }

  // ── Load / save ───────────────────────────────────────────────────────────

  async _load() {
    try {
      const layout = await this._hass.callWS({ type: "display/layout/get", device_id: this._deviceId });
      this._widgets = layout.widgets;
      this._selected = null;
      this._setDirty(false);
      this._message("");
      this._draw();
      this._renderProps();
      this._refreshDot();
    } catch (err) {
      this._message(`Layout konnte nicht geladen werden: ${err.message || err.code}`, true);
    }
  }

  async _reload() {
    if (this._dirty && !confirm("Ungespeicherte Änderungen verwerfen?")) return;
    await this._load();
  }

  async _switchDevice(id) {
    if (this._dirty && !confirm("Ungespeicherte Änderungen verwerfen?")) {
      this.shadowRoot.getElementById("dev").value = this._deviceId;
      return;
    }
    this._deviceId = id;
    this.shadowRoot.getElementById("setupbox").innerHTML = "";
    await this._load();
  }

  async _save() {
    const btn = this.shadowRoot.getElementById("save");
    btn.disabled = true;
    try {
      const res = await this._hass.callWS({ type: "display/layout/set", device_id: this._deviceId, widgets: this._widgets });
      this._widgets = res.widgets;
      this._draw();
      this._renderProps();
      this._setDirty(false);
      this._message("Gespeichert – das Display übernimmt das Layout sofort.");
      setTimeout(() => this._message(""), 3000);
    } catch (err) {
      this._message(`Nicht gespeichert: ${err.message || err.code}`, true);
      btn.disabled = false;
    }
  }

  _setDirty(dirty) {
    this._dirty = dirty;
    const btn = this.shadowRoot.getElementById("save");
    if (btn) { btn.disabled = !dirty; btn.textContent = dirty ? "Speichern •" : "Speichern"; }
  }

  _message(text, error = false) {
    const el = this.shadowRoot.getElementById("msg");
    if (el) el.innerHTML = text ? `<div class="msg ${error ? "err" : ""}">${esc(text)}</div>` : "";
  }

  _refreshDot() {
    const dev = this._devices.find((d) => d.id === this._deviceId);
    const dot = this.shadowRoot.getElementById("dot");
    if (!dev || !dot) return;
    const sensor = Object.values(this._hass.states).find((s) => s.attributes.device_class === "connectivity" && s.entity_id.endsWith("_connected") && s.attributes.friendly_name?.startsWith(dev.name));
    dot.classList.toggle("on", sensor ? sensor.state === "on" : dev.online);
  }

  // ── Panel setup info (address + this display's own device code) ───────────

  async _toggleSetup() {
    const box = this.shadowRoot.getElementById("setupbox");
    if (box.innerHTML) { box.innerHTML = ""; return; }
    try {
      const info = await this._hass.callWS({ type: "display/setup_info", device_id: this._deviceId });
      const link = `http://192.168.4.1/?url=${encodeURIComponent(info.ha_url)}&key=${encodeURIComponent(info.device_key)}`;
      const rows = [["Home-Assistant-URL", info.ha_url, "text"], ["Gerätecode (nur für dieses Display)", info.device_key, "password"], ["Einrichtungslink", link, "password"]];
      box.innerHTML = `<div class="setup">
        <p class="hint">Mit dem WLAN „ESP32-Display-…“ des Displays verbinden (Passwort steht auf dem Display) und den <b>Einrichtungslink</b> im Browser öffnen – URL und Gerätecode sind dann schon eingetragen, es fehlt nur dein WLAN. Alternativ http://192.168.4.1 öffnen und die Werte einfügen.</p>
        ${rows.map(([label, value, type], i) => `
          <div class="row"><span style="width:170px;flex:none">${esc(label)}</span><input readonly type="${type}" value="${esc(value)}" id="si${i}">
          ${type === "password" ? `<button class="btn" data-show="si${i}">Anzeigen</button>` : ""}<button class="btn" data-copy="si${i}">Kopieren</button></div>`).join("")}
      </div>`;
      box.querySelectorAll("[data-show]").forEach((b) => b.addEventListener("click", () => {
        const input = box.querySelector(`#${b.dataset.show}`);
        input.type = input.type === "password" ? "text" : "password";
      }));
      box.querySelectorAll("[data-copy]").forEach((b) => b.addEventListener("click", async () => {
        const input = box.querySelector(`#${b.dataset.copy}`);
        try { await navigator.clipboard.writeText(input.value); b.textContent = "Kopiert"; }
        catch { const t = input.type; input.type = "text"; input.select(); input.type = t; b.textContent = "Markiert – Strg+C"; }
      }));
    } catch (err) {
      this._message(err.code === "unauthorized" ? "Die Einrichtungsdaten sehen nur Administratoren." : `Fehler: ${err.message}`, true);
    }
  }

  // ── Widgets ───────────────────────────────────────────────────────────────

  _nextId() {
    const used = new Set(this._widgets.map((w) => w.id));
    let n = 1;
    while (used.has(`w${n}`)) n++;
    return `w${n}`;
  }

  _add(type, x, y) {
    if (this._widgets.length >= MAX_WIDGETS) return this._message(`Mehr als ${MAX_WIDGETS} Elemente passen nicht auf das Display.`, true);
    const d = TYPES[type];
    const w = { id: this._nextId(), type, entity: "", label: "", unit: type === "bar" ? "%" : "", min: 0, max: 100, w: d.w, h: d.h, x: 0, y: 0 };
    w.x = clamp(snap(x), 0, SCREEN_W - w.w);
    w.y = clamp(snap(y), 0, SCREEN_H - w.h);
    this._widgets.push(w);
    this._setDirty(true);
    this._draw();
    this._select(w.id);
  }

  _remove(id) {
    this._widgets = this._widgets.filter((w) => w.id !== id);
    this._setDirty(true);
    this._select(null);
    this._draw();
  }

  _find(id) { return this._widgets.find((w) => w.id === id); }

  _select(id) {
    this._selected = id;
    for (const w of this._widgets) {
      const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
      if (el) this._paint(el, w);
    }
    this._renderProps();
  }

  _key(e) {
    const w = this._find(this._selected);
    if (!w) return;
    if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); return this._remove(w.id); }
    const step = e.shiftKey ? GRID : 1;
    const move = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[e.key];
    if (!move) return;
    e.preventDefault();
    w.x = clamp(w.x + move[0], 0, SCREEN_W - w.w);
    w.y = clamp(w.y + move[1], 0, SCREEN_H - w.h);
    this._place(w);
    this._setDirty(true);
    this._syncProps();
  }

  // ── Canvas ────────────────────────────────────────────────────────────────

  _draw() {
    const canvas = this.shadowRoot.getElementById("canvas");
    if (!canvas) return;
    canvas.innerHTML = "";
    for (const w of this._widgets) {
      const el = document.createElement("div");
      el.dataset.id = w.id;
      canvas.appendChild(el);
      this._paint(el, w);
      this._drag(el, w);
    }
    this._sig = this._signature();
  }

  _place(w) {
    const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
    if (!el) return;
    Object.assign(el.style, { left: `${w.x}px`, top: `${w.y}px`, width: `${w.w}px`, height: `${w.h}px` });
  }

  _state(w) { return w.entity ? this._hass?.states?.[w.entity]?.state : undefined; }

  _paint(el, w) {
    el.className = `w ${w.type}${w.id === this._selected ? " sel" : ""}`;
    this._place(w);
    const title = esc(w.label || w.entity || TYPES[w.type].name);
    const state = this._state(w);
    const n = parseFloat(state);
    const isNum = Number.isFinite(n);
    const unit = esc(w.unit);
    const range = w.max > w.min ? w.max - w.min : 100;
    const pct = isNum ? clamp(((n - w.min) / range) * 100, 0, 100) : 0;
    const on = state === "on";
    let inner;
    switch (w.type) {
      case "button":
        el.classList.toggle("on", on);
        inner = `<div class="v">${title}</div>`; break;
      case "gauge": {
        const size = Math.max(10, Math.min(w.w, w.h) - 34);
        inner = `<div class="t">${title}</div><div class="ring" style="width:${size}px;height:${size}px;background:conic-gradient(from 225deg,#4fc3f7 ${pct * 2.7}deg,#2a2a44 0 270deg,transparent 0)"></div><div class="v">${isNum ? n.toFixed(1) + unit : "--"}</div>`; break;
      }
      case "switch":
        inner = `<div class="t">${title}</div><div class="pill${on ? " on" : ""}"></div>`; break;
      case "bar":
        inner = `<div class="t">${title}</div><div class="v">${isNum ? Math.round(n) + unit : "--"}</div><div class="track"><i style="width:${pct}%"></i></div>`; break;
      default:
        inner = `<div class="t">${title}</div><div class="v">${state === undefined ? "--" : esc(state) + (unit ? " " + unit : "")}</div>`;
    }
    el.innerHTML = inner + (w.id === this._selected ? '<div class="rz"></div>' : "");
  }

  _signature() {
    return this._widgets.map((w) => `${w.id}:${this._state(w)}`).join("|");
  }

  _refreshValues() {
    if (!this.shadowRoot.getElementById("canvas")) return;
    const sig = this._signature();
    if (sig !== this._sig) {
      this._sig = sig;
      for (const w of this._widgets) {
        const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
        if (el) this._paint(el, w);
      }
    }
    this._refreshDot();
  }

  _drag(el, w) {
    el.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.stopPropagation();
      const resizing = e.target.classList.contains("rz");
      if (this._selected !== w.id) this._select(w.id);
      this.shadowRoot.getElementById("screen").focus({ preventScroll: true });
      const start = { x: e.clientX, y: e.clientY, wx: w.x, wy: w.y, ww: w.w, wh: w.h };
      let moved = false;
      const onMove = (m) => {
        const dx = (m.clientX - start.x) / this._scale, dy = (m.clientY - start.y) / this._scale;
        if (!moved && Math.hypot(dx, dy) < 3) return;
        moved = true;
        if (resizing) {
          w.w = clamp(snap(start.ww + dx), MIN_SIZE, SCREEN_W - w.x);
          w.h = clamp(snap(start.wh + dy), MIN_SIZE, SCREEN_H - w.y);
          this._paint(el, w);
        } else {
          w.x = clamp(snap(start.wx + dx), 0, SCREEN_W - w.w);
          w.y = clamp(snap(start.wy + dy), 0, SCREEN_H - w.h);
          this._place(w);
        }
        this._setDirty(true);
        this._syncProps();
      };
      const onUp = () => { window.removeEventListener("pointermove", onMove); window.removeEventListener("pointerup", onUp); };
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
  }

  // ── Properties ────────────────────────────────────────────────────────────

  _entityIds(type) {
    const domains = TYPES[type].domains;
    return Object.keys(this._hass.states).filter((id) => !domains || domains.includes(id.split(".")[0])).sort();
  }

  _renderProps() {
    const panel = this.shadowRoot.getElementById("props");
    if (!panel) return;
    const w = this._find(this._selected);
    if (!w) {
      panel.innerHTML = `<div class="empty">Element aus der Palette ziehen oder anklicken.<br>Auf dem Display auswählen, dann Entität und Beschriftung festlegen.<br><br>${this._widgets.length} von ${MAX_WIDGETS} Elementen.<br>Pfeiltasten verschieben, Entf löscht.</div>`;
      return;
    }
    const numeric = w.type === "gauge" || w.type === "bar";
    panel.innerHTML = `<h3>${TYPES[w.type].name}</h3>
      <div class="field"><label>Entität</label><input id="p-entity" list="p-ents" value="${esc(w.entity)}" placeholder="sensor.temperatur" autocomplete="off"><datalist id="p-ents">${this._entityIds(w.type).map((id) => `<option value="${esc(id)}">`).join("")}</datalist></div>
      <div class="field"><label>Beschriftung</label><input id="p-label" value="${esc(w.label)}" maxlength="31" placeholder="${esc(this._hass.states[w.entity]?.attributes.friendly_name || "")}"></div>
      ${w.type === "label" || numeric ? `<div class="field"><label>Einheit</label><input id="p-unit" value="${esc(w.unit)}" maxlength="15" placeholder="°C"></div>` : ""}
      ${numeric ? `<div class="two"><div class="field"><label>Min</label><input id="p-min" type="number" value="${w.min}"></div><div class="field"><label>Max</label><input id="p-max" type="number" value="${w.max}"></div></div>` : ""}
      <div class="two"><div class="field"><label>X</label><input id="p-x" type="number" value="${w.x}"></div><div class="field"><label>Y</label><input id="p-y" type="number" value="${w.y}"></div></div>
      <div class="two"><div class="field"><label>Breite</label><input id="p-w" type="number" value="${w.w}"></div><div class="field"><label>Höhe</label><input id="p-h" type="number" value="${w.h}"></div></div>
      <button class="btn danger" id="p-del">Element löschen</button>`;
    const bind = (id, apply) => panel.querySelector(id)?.addEventListener("input", (e) => {
      apply(e.target.value);
      this._setDirty(true);
      const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
      if (el) this._paint(el, w);
    });
    bind("#p-entity", (v) => { w.entity = v.trim(); this._sig = ""; });
    bind("#p-label", (v) => { w.label = v; });
    bind("#p-unit", (v) => { w.unit = v; });
    bind("#p-min", (v) => { w.min = Math.round(num(v, 0)); });
    bind("#p-max", (v) => { w.max = Math.round(num(v, 100)); });
    bind("#p-x", (v) => { w.x = clamp(Math.round(num(v, 0)), 0, SCREEN_W - w.w); });
    bind("#p-y", (v) => { w.y = clamp(Math.round(num(v, 0)), 0, SCREEN_H - w.h); });
    bind("#p-w", (v) => { w.w = clamp(Math.round(num(v, w.w)), MIN_SIZE, SCREEN_W - w.x); });
    bind("#p-h", (v) => { w.h = clamp(Math.round(num(v, w.h)), MIN_SIZE, SCREEN_H - w.y); });
    panel.querySelector("#p-del").addEventListener("click", () => this._remove(w.id));
  }

  _syncProps() {
    const w = this._find(this._selected);
    if (!w) return;
    for (const k of ["x", "y", "w", "h"]) {
      const input = this.shadowRoot.getElementById(`p-${k}`);
      if (input && document.activeElement !== input) input.value = w[k];
    }
  }
}

class DisplayCardEditor extends HTMLElement {
  set hass(hass) {
    this._hass = hass;
    if (!this._loaded) { this._loaded = true; this._load(); }
  }

  setConfig(config) { this._config = config || {}; this._render(); }

  async _load() {
    try { this._names = (await this._hass.callWS({ type: "display/devices" })).devices.map((d) => d.name); }
    catch { this._names = []; }
    this._render();
  }

  _render() {
    if (!this._config) return;
    const names = this._names || [];
    this.innerHTML = `<div style="padding:16px;font-family:var(--paper-font-body1_-_font-family,sans-serif)">
      <label style="font-size:12px;color:var(--secondary-text-color)">Display (leer = Auswahl in der Karte)</label>
      <select style="width:100%;margin-top:4px;padding:8px;border:1px solid var(--divider-color);border-radius:6px;background:var(--secondary-background-color);color:var(--primary-text-color)">
        <option value="">– in der Karte wählen –</option>${names.map((n) => `<option value="${esc(n)}" ${n === this._config.device ? "selected" : ""}>${esc(n)}</option>`).join("")}
      </select></div>`;
    this.querySelector("select").addEventListener("change", (e) => {
      const config = { ...this._config };
      if (e.target.value) config.device = e.target.value; else delete config.device;
      this.dispatchEvent(new CustomEvent("config-changed", { detail: { config }, bubbles: true, composed: true }));
    });
  }
}

customElements.define("display-card", DisplayCard);
customElements.define("display-card-editor", DisplayCardEditor);

window.customCards = window.customCards || [];
window.customCards.push({
  type: "display-card",
  name: "ESP32 Display",
  description: `Layout-Editor für ESP32-Display-Panels (v${CARD_VERSION})`,
});
