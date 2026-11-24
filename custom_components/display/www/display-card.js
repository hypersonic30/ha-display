// ESP32 Display card: visual layout editor for the panels set up in the "ESP32 Display" integration.
// Talks to Home Assistant only through the integration's WebSocket commands (display/...).

const CARD_VERSION = "0.6.2";
const GRID = 5, MAX_WIDGETS = 32, MAX_PAGES = 8, MIN_SIZE = 20, PAGER_MIN_W = 60, PAGER_MIN_H = 24;
const PAGER_ID = "__pager";

const ACT = ["light", "switch", "input_boolean", "automation", "script", "scene", "button", "input_button", "fan", "cover", "media_player", "climate", "vacuum"];
const TOGGLE = ["light", "switch", "input_boolean", "fan", "automation", "cover", "media_player"];
const SLIDE = ["light", "cover", "fan", "media_player", "input_number", "number"];
const SELECT = ["select", "input_select", "media_player"];
const TYPES = {
  label:      { icon: "Aa",  name: "Wert",        w: 180, h: 70,  domains: null },
  button:     { icon: "▣",   name: "Taster",      w: 130, h: 80,  domains: ACT },
  switch:     { icon: "⇆",   name: "Schalter",    w: 170, h: 60,  domains: TOGGLE },
  checkbox:   { icon: "☑",   name: "Kästchen",    w: 180, h: 44,  domains: TOGGLE },
  slider:     { icon: "◐",   name: "Schieber",    w: 220, h: 60,  domains: SLIDE },
  arc:        { icon: "◠",   name: "Drehregler",  w: 140, h: 140, min: [60, 60], domains: SLIDE },
  gauge:      { icon: "◔",   name: "Anzeige",     w: 150, h: 150, domains: ["sensor", "input_number", "number"] },
  bar:        { icon: "▬",   name: "Balken",      w: 210, h: 60,  domains: ["sensor", "input_number", "number"] },
  led:        { icon: "●",   name: "LED",         w: 60,  h: 60,  domains: null },
  dropdown:   { icon: "▾",   name: "Auswahl",     w: 200, h: 50,  domains: SELECT },
  roller:     { icon: "☰",   name: "Rad",         w: 160, h: 110, domains: SELECT },
  clock:      { icon: "◷",   name: "Uhr",         w: 160, h: 70,  domains: [] },
  panel:      { icon: "▭",   name: "Fläche",      w: 200, h: 120, domains: [] },
  media:      { icon: "♫",   name: "Medien",      w: 300, h: 120, min: [200, 100], domains: ["media_player"] },
  thermostat: { icon: "☼",   name: "Thermostat",  w: 200, h: 110, min: [140, 90], domains: ["climate"] },
  pager:      { icon: "‹ ›", name: "Seitenwechsel", w: 220, h: 40, domains: null },
};
const minSize = (type) => TYPES[type]?.min || [MIN_SIZE, MIN_SIZE];
const ACTIVE = new Set(["on", "open", "opening", "closing", "playing", "home", "unlocked", "heat", "cool", "heat_cool", "auto", "active", "detected", "armed_home", "armed_away", "cleaning"]);
const COL = { off: "#2a2a44", on: "#4fc3f7", card: "#1a1a2e", text: "#e0e0e0", muted: "#777799" };
const ACTIONS = {
  media_player: [["play_pause", "Wiedergabe / Pause"], ["play", "Wiedergabe"], ["pause", "Pause"], ["stop", "Stopp"], ["next", "Nächster Titel"], ["prev", "Vorheriger Titel"], ["volume_up", "Lauter"], ["volume_down", "Leiser"], ["mute", "Stumm schalten"]],
  cover: [["toggle", "Umschalten"], ["open", "Öffnen"], ["close", "Schließen"], ["stop", "Stopp"]],
  climate: [["temp_up", "Wärmer"], ["temp_down", "Kälter"]],
  vacuum: [["start", "Start"], ["pause", "Pause"], ["return_home", "Zur Station"]],
};
const ACTION_ICON = { play_pause: "play", play: "play", pause: "pause", stop: "stop", next: "next", prev: "prev", volume_up: "volume", volume_down: "volume", mute: "mute", temp_up: "plus", temp_down: "minus", open: "up", close: "down", start: "play", return_home: "home" };
const ICONS = {
  power: ["⏻", "Ein/Aus"], home: ["⌂", "Haus"], bell: ["🔔", "Glocke"], play: ["▶", "Play"], pause: ["⏸", "Pause"], stop: ["⏹", "Stopp"],
  next: ["⏭", "Weiter"], prev: ["⏮", "Zurück"], plus: ["+", "Plus"], minus: ["−", "Minus"], up: ["▲", "Hoch"], down: ["▼", "Runter"],
  left: ["◀", "Links"], right: ["▶", "Rechts"], ok: ["✓", "Haken"], close: ["✕", "Kreuz"], settings: ["⚙", "Zahnrad"], refresh: ["↻", "Aktualisieren"],
  wifi: ["📶", "WLAN"], battery: ["🔋", "Batterie"], charge: ["⚡", "Laden"], warning: ["⚠", "Warnung"], eye: ["👁", "Auge"], volume: ["🔊", "Lautstärke"],
  mute: ["🔇", "Stumm"], audio: ["♪", "Musik"], video: ["🎬", "Video"], list: ["☰", "Liste"], trash: ["🗑", "Papierkorb"], edit: ["✎", "Stift"],
  gps: ["⌖", "Ort"], image: ["🖼", "Bild"], shuffle: ["🔀", "Zufall"], loop: ["🔁", "Wiederholen"], bluetooth: ["ᛒ", "Bluetooth"], usb: ["⭥", "USB"],
};
const FONTS = [12, 14, 16, 20, 24];
const FONT_TYPES = ["label", "button", "switch", "checkbox", "dropdown", "roller", "media", "thermostat", "clock"];
const fontStyle = (w) => (w.font > 0 ? `font-size:${w.font}px;` : "");
const FORMATS = [["time", "Uhrzeit 14:05"], ["time_s", "Uhrzeit mit Sekunden"], ["date", "Datum 24.11.2026"], ["datetime", "Datum und Uhrzeit"]];
const NO_ENTITY = (type) => TYPES[type]?.domains?.length === 0;
const levelColor = (w, n, base) => {
  if (!Number.isFinite(n)) return base;
  const hit = (t) => t !== null && t !== undefined && t !== "" && (w.invert ? n <= Number(t) : n >= Number(t));
  return hit(w.crit) ? "#ef5350" : hit(w.warn) ? "#ffa726" : base;
};
const clockText = (format) => {
  const d = new Date(), p = (x) => String(x).padStart(2, "0"), t = `${p(d.getHours())}:${p(d.getMinutes())}`;
  return format === "time_s" ? `${t}:${p(d.getSeconds())}` : format === "date" ? `${p(d.getDate())}.${p(d.getMonth() + 1)}.${d.getFullYear()}`
    : format === "datetime" ? `${p(d.getDate())}.${p(d.getMonth() + 1)}. ${t}` : t;
};

const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const snap = (v) => Math.round(v / GRID) * GRID;
// The firmware stores texts in fixed byte buffers: cut at a byte limit (umlauts count double), never inside a character.
const cutBytes = (s, max) => { let out = ""; for (const ch of String(s)) { if (new TextEncoder().encode(out + ch).length > max) break; out += ch; } return out; };
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
.canvas { position: absolute; left: 0; top: 0; width: 480px; height: 320px; transform-origin: 0 0;
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
.w.pager { display: flex; align-items: center; padding: 0; background: #1a1a2e; }
.w.pager .ar { flex: none; text-align: center; font-size: 24px; color: #4fc3f7; pointer-events: none; }
.w.pager .pl { flex: 1; min-width: 0; text-align: center; font-size: 16px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; pointer-events: none; }
.w.pager.idle:not(.sel) { opacity: .55; border: 2px dashed #4fc3f788; }
.w.led { background: transparent; padding: 0; display: flex; align-items: center; justify-content: center; }
.led i { display: block; border-radius: 50%; }
.w.panel { padding: 6px 8px; }
.w.clock { display: flex; align-items: center; justify-content: center; font-size: 30px; }
.strack { position: absolute; left: 14px; right: 14px; bottom: 12px; height: 8px; border-radius: 4px; pointer-events: none; }
.strack i { display: block; height: 100%; border-radius: 4px; } .strack b { position: absolute; top: -6px; width: 20px; height: 20px; margin-left: -10px; border-radius: 50%; background: #fff; }
.box { position: absolute; left: 8px; right: 8px; bottom: 6px; top: 24px; border-radius: 8px; display: flex; align-items: center; padding: 0 10px; pointer-events: none; font-size: 15px; justify-content: space-between; gap: 6px; }
.box span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.chk { position: absolute; left: 8px; top: 50%; transform: translateY(-50%); width: 24px; height: 24px; border-radius: 6px; display: flex; align-items: center; justify-content: center; font-size: 16px; color: #0f0f1a; pointer-events: none; }
.w.checkbox .t { position: absolute; left: 40px; top: 50%; transform: translateY(-50%); right: 8px; font-size: 14px; color: inherit; }
.w.media .t, .w.thermostat .t { font-size: 15px; color: inherit; }
.w.media .a { font-size: 12px; color: #777799; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; pointer-events: none; }
.ctl { position: absolute; left: 8px; right: 8px; display: flex; justify-content: space-around; font-size: 22px; pointer-events: none; }
.thermo { position: absolute; inset: 24px 8px 6px; display: flex; align-items: center; justify-content: space-between; pointer-events: none; }
.thermo big { font-size: 30px; } .thermo em { font-style: normal; font-size: 28px; padding: 0 10px; border-radius: 8px; background: #ffffff18; }
.color { display: flex; gap: 6px; align-items: center; } .color input[type=color] { width: 44px; height: 30px; padding: 0; border: 1px solid var(--divider-color); border-radius: 6px; background: none; flex: none; }
.color input.unset { opacity: .45; } .mini { border: 0; background: none; color: var(--secondary-text-color); font-size: 12px; cursor: pointer; text-decoration: underline; padding: 2px; } .mini:disabled { opacity: .35; cursor: default; text-decoration: none; }
.check { display: flex; gap: 6px; align-items: center; font-size: 13px; margin-bottom: 9px; }
.pagebar { display: flex; flex-wrap: wrap; gap: 6px; padding: 8px 14px; border-bottom: 1px solid var(--divider-color); align-items: center; }
.chip { border: 1px solid var(--divider-color); background: var(--secondary-background-color); color: var(--primary-text-color); border-radius: 16px; padding: 5px 14px; font-size: 13px; cursor: pointer; max-width: 160px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.chip.on { background: var(--primary-color); color: var(--text-primary-color, #fff); border-color: transparent; }
.chip.add { padding: 5px 11px; font-size: 15px; } .chip:disabled { opacity: .4; cursor: default; }
.dim { color: var(--secondary-text-color); font-size: 12px; }
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
    this._pages = [{ id: "p1", name: "Seite 1", widgets: [] }];
    this._page = 0;
    this._pager = null;
    this._screen = { w: 480, h: 320 };
    this._selected = null;
    this._dirty = false;
    this._scale = 1;
    this._sig = "";
  }

  get _W() { return this._screen.w; }
  get _H() { return this._screen.h; }
  get _widgets() { return this._pages[this._page].widgets; }
  set _widgets(v) { this._pages[this._page].widgets = v; }

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
          <span class="dim" id="res"></span>
          <span class="dim" id="fw"></span><button class="btn" id="fwbtn" hidden>Aktualisieren</button>
          <button class="btn" id="setup">Einrichtung</button>
          <button class="btn" id="reload" title="Gespeicherten Stand neu laden">↻</button>
          <button class="btn primary" id="save" disabled>Speichern</button>
        </div>
        <div id="setupbox"></div>
        <div id="msg"></div>
        <div class="pagebar" id="pages"></div>
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
    $("fwbtn").addEventListener("click", () => this._updateFirmware());
    this.shadowRoot.querySelectorAll(".pi").forEach((el) => {
      el.addEventListener("dragstart", (e) => e.dataTransfer.setData("text/plain", el.dataset.type));
      el.addEventListener("click", () => this._add(el.dataset.type, 20, 20, true));
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
    this._stage = this.shadowRoot.querySelector(".stage");
    new ResizeObserver(() => this._fit(this._stage)).observe(this._stage);
    this._renderPages();
    this._renderProps();
  }

  _fit(stage) {
    const avail = stage.clientWidth - 28;
    if (avail <= 0) return;
    this._scale = Math.min(1.6, avail / this._W, 640 / this._H);
    const screen = this.shadowRoot.getElementById("screen");
    screen.style.width = `${this._W * this._scale}px`;
    screen.style.height = `${this._H * this._scale}px`;
    const canvas = this.shadowRoot.getElementById("canvas");
    canvas.style.width = `${this._W}px`;
    canvas.style.height = `${this._H}px`;
    canvas.style.transform = `scale(${this._scale})`;
  }

  // ── Load / save ───────────────────────────────────────────────────────────

  async _load() {
    try {
      const layout = await this._hass.callWS({ type: "display/layout/get", device_id: this._deviceId });
      this._applyLayout(layout);
      this._selected = null;
      this._setDirty(false);
      this._message("");
      this._renderPages();
      this._draw();
      this._renderProps();
      this._refreshDot();
    } catch (err) {
      this._message(`Layout konnte nicht geladen werden: ${err.message || err.code}`, true);
    }
  }

  _applyLayout(layout) {
    this._pages = layout.pages.map((p) => ({ id: p.id, name: p.name, bg: p.bg || "", widgets: p.widgets.map((w) => ({ ...w })) }));
    this._pager = layout.pager ? { ...layout.pager, id: PAGER_ID, type: "pager" } : null;
    this._page = Math.min(this._page, this._pages.length - 1);
    this._screen = layout.screen;
    const res = this.shadowRoot.getElementById("res");
    if (res) res.textContent = `${this._W}×${this._H} ${this._W > this._H ? "Querformat" : "Hochformat"}`;
    if (this._stage) this._fit(this._stage);
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
    this._page = 0;
    this.shadowRoot.getElementById("setupbox").innerHTML = "";
    await this._load();
  }

  async _save() {
    const btn = this.shadowRoot.getElementById("save");
    btn.disabled = true;
    try {
      const pager = this._pager ? { x: this._pager.x, y: this._pager.y, w: this._pager.w, h: this._pager.h, mode: this._pager.mode } : null;
      const res = await this._hass.callWS({ type: "display/layout/set", device_id: this._deviceId, pages: this._pages, pager });
      this._applyLayout(res);
      this._renderPages();
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
    // The entity id depends on the language Home Assistant ran in when the display was added (…_connected, …_verbunden).
    const sensor = dev.connected_entity ? this._hass.states[dev.connected_entity] : null;
    this._online = sensor ? sensor.state === "on" : dev.online;
    dot.classList.toggle("on", this._online);
    this._refreshFirmware();
  }

  // ── Firmware (over the air, through the integration's update entity) ──────

  _refreshFirmware() {
    const text = this.shadowRoot.getElementById("fw");
    const button = this.shadowRoot.getElementById("fwbtn");
    if (!text || !button) return;
    const dev = this._devices.find((d) => d.id === this._deviceId);
    const state = dev?.update_entity ? this._hass.states[dev.update_entity] : null;
    if (!state) { text.textContent = ""; button.hidden = true; return; }
    const a = state.attributes;
    let label, available = false;
    if (a.in_progress) label = `Update läuft … ${a.update_percentage ?? 0} %`;
    else if (!a.installed_version) label = this._online ? "Firmware älter als 0.3.0: einmal per Kabel flashen" : "Firmware unbekannt (Display nicht verbunden)";
    else if (state.state === "on") { label = `Firmware ${a.installed_version} → ${a.latest_version} verfügbar`; available = true; }
    else label = `Firmware ${a.installed_version}`;
    text.textContent = label;
    button.hidden = !available && !a.in_progress;
    button.disabled = !!a.in_progress || !this._online;
    button.title = this._online ? "" : "Das Display muss eingeschaltet und verbunden sein";
  }

  async _updateFirmware() {
    const dev = this._devices.find((d) => d.id === this._deviceId);
    const a = this._hass.states[dev?.update_entity]?.attributes;
    if (!a || !confirm(`Firmware von „${dev.name}“ auf ${a.latest_version} aktualisieren?\n\nDas Display lädt die neue Firmware und startet danach neu. In dieser Zeit nicht ausschalten.`)) return;
    this._message("Update gestartet …");
    try {
      await this._hass.callService("update", "install", { entity_id: dev.update_entity });
      this._message("Firmware aktualisiert.");
      setTimeout(() => this._message(""), 4000);
    } catch (err) {
      this._message(`Update fehlgeschlagen: ${err.message || err.code}`, true);
    }
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

  _add(type, x, y, click = false) {
    if (type === "pager") return this._addPager(x, y, click);
    if (this._widgets.length >= MAX_WIDGETS) return this._message(`Mehr als ${MAX_WIDGETS} Elemente passen nicht auf eine Seite.`, true);
    const d = TYPES[type];
    const w = {
      id: this._nextId(), type, entity: "", label: "", unit: type === "bar" ? "%" : "", min: 0, max: 100, w: d.w, h: d.h, x: 0, y: 0,
      bg: "", fg: "", on: "", attr: "", icon: "", action: "", format: "time", font: 0, warn: null, crit: null, invert: false,
    };
    w.x = clamp(snap(x), 0, this._W - w.w);
    w.y = clamp(snap(y), 0, this._H - w.h);
    this._widgets.push(w);
    this._setDirty(true);
    this._draw();
    this._select(w.id);
  }

  _addPager(x, y, click) {
    if (!this._pager) {
      const d = TYPES.pager;
      const w = Math.min(d.w, this._W);
      const placed = click;                          // palette click: put it at the bottom, centred
      this._pager = {
        id: PAGER_ID, type: "pager", mode: "name", w, h: d.h,
        x: clamp(snap(placed ? (this._W - w) / 2 : x), 0, this._W - w),
        y: clamp(snap(placed ? this._H - d.h - 10 : y), 0, this._H - d.h),
      };
      this._setDirty(true);
      this._draw();
    }
    this._select(PAGER_ID);
  }

  _nextPageId() {
    const used = new Set(this._pages.map((p) => p.id));
    let n = 1;
    while (used.has(`p${n}`)) n++;
    return `p${n}`;
  }

  _renderPages() {
    const box = this.shadowRoot.getElementById("pages");
    if (!box) return;
    box.innerHTML = this._pages.map((p, i) => `<button class="chip${i === this._page ? " on" : ""}" data-page="${i}" title="${esc(p.name)}">${esc(p.name)}</button>`).join("")
      + `<button class="chip add" id="pg-add" title="Seite hinzufügen" ${this._pages.length >= MAX_PAGES ? "disabled" : ""}>+</button>`;
    box.querySelectorAll("[data-page]").forEach((b) => b.addEventListener("click", () => this._gotoPage(+b.dataset.page)));
    box.querySelector("#pg-add").addEventListener("click", () => this._addPage());
  }

  _gotoPage(i) {
    this._page = clamp(i, 0, this._pages.length - 1);
    this._selected = null;
    this._renderPages();
    this._draw();
    this._renderProps();
  }

  _addPage(copy = false) {
    if (this._pages.length >= MAX_PAGES) return;
    const from = this._pages[this._page];
    this._pages.splice(this._page + 1, 0, {
      id: this._nextPageId(),
      name: cutBytes(copy ? `${from.name} 2` : `Seite ${this._pages.length + 1}`, 31),
      bg: copy ? from.bg || "" : "",
      widgets: copy ? from.widgets.map((w) => ({ ...w })) : [],
    });
    this._setDirty(true);
    this._gotoPage(this._page + 1);
  }

  _deletePage() {
    if (this._pages.length < 2) return;
    if (this._widgets.length && !confirm(`Seite „${this._pages[this._page].name}“ mit ${this._widgets.length} Elementen löschen?`)) return;
    this._pages.splice(this._page, 1);
    this._setDirty(true);
    this._gotoPage(Math.min(this._page, this._pages.length - 1));
  }

  _movePage(dir) {
    const to = this._page + dir;
    if (to < 0 || to >= this._pages.length) return;
    [this._pages[this._page], this._pages[to]] = [this._pages[to], this._pages[this._page]];
    this._setDirty(true);
    this._gotoPage(to);
  }

  _remove(id) {
    if (id === PAGER_ID) {
      this._pager = null;
      this._setDirty(true);
      this._select(null);
      this._draw();
      return;
    }
    this._widgets = this._widgets.filter((w) => w.id !== id);
    this._setDirty(true);
    this._select(null);
    this._draw();
  }

  _find(id) { return id === PAGER_ID ? this._pager : this._widgets.find((w) => w.id === id); }

  _select(id) {
    this._selected = id;
    for (const w of this._items()) {
      const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
      if (el) this._paint(el, w);
    }
    this._renderProps();
  }

  _items() { return this._pager ? [...this._widgets, this._pager] : this._widgets; }

  _key(e) {
    const w = this._find(this._selected);
    if (!w) return;
    if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); return this._remove(w.id); }
    const step = e.shiftKey ? GRID : 1;
    const move = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[e.key];
    if (!move) return;
    e.preventDefault();
    w.x = clamp(w.x + move[0], 0, this._W - w.w);
    w.y = clamp(w.y + move[1], 0, this._H - w.h);
    this._place(w);
    this._setDirty(true);
    this._syncProps();
  }

  // ── Canvas ────────────────────────────────────────────────────────────────

  _draw() {
    const canvas = this.shadowRoot.getElementById("canvas");
    if (!canvas) return;
    canvas.innerHTML = "";
    this.shadowRoot.getElementById("screen").style.background = this._pages[this._page].bg || "";
    for (const w of this._items()) {
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

  _paintPager(el, w) {
    const alone = this._pages.length < 2;
    el.className = `w pager${w.id === this._selected ? " sel" : ""}${alone ? " idle" : ""}`;
    this._place(w);
    const arrow = Math.max(24, Math.min(w.h * 1.2, w.w / 3));
    const label = w.mode === "count" ? `${this._page + 1} / ${this._pages.length}` : esc(this._pages[this._page].name);
    el.title = alone ? "Am Display erst ab 2 Seiten sichtbar" : "";
    el.innerHTML = `<span class="ar" style="width:${arrow}px">&lt;</span><span class="pl">${label}</span><span class="ar" style="width:${arrow}px">&gt;</span>`
      + (w.id === this._selected ? '<div class="rz"></div>' : "");
  }

  // The text a widget shows: the state, or an attribute of the entity.
  _text(w) {
    const st = this._hass?.states?.[w.entity];
    if (!st) return undefined;
    if (!w.attr) return st.state;
    const v = st.attributes?.[w.attr];
    return v === undefined || v === null ? "" : Array.isArray(v) ? v.join(", ") : String(v);
  }

  // Slider/arc value in the widget's scale (same rules as the integration).
  _level(w) {
    const st = this._hass?.states?.[w.entity];
    if (!st) return NaN;
    const a = st.attributes || {}, d = w.entity.split(".")[0];
    if (d === "light") return st.state === "on" && a.brightness != null ? Math.round((a.brightness * 100) / 255) : 0;
    if (d === "cover") return Number(a.current_position) || 0;
    if (d === "fan") return Number(a.percentage) || 0;
    if (d === "media_player") return typeof a.volume_level === "number" ? Math.round(a.volume_level * 100) : 0;
    return parseFloat(st.state);
  }

  _paint(el, w) {
    if (w.type === "pager") return this._paintPager(el, w);
    el.className = `w ${w.type}${w.id === this._selected ? " sel" : ""}`;
    el.style.background = "";
    el.style.color = "";
    this._place(w);
    const st = this._hass?.states?.[w.entity];
    const attrs = st?.attributes || {};
    const state = st?.state;
    const active = ACTIVE.has(String(state).toLowerCase());
    const off = w.bg || COL.off, onc = w.on || COL.on;
    if (w.fg) el.style.color = w.fg;
    const ic = w.icon && ICONS[w.icon] ? ICONS[w.icon][0] : "";
    const title = esc(w.label || w.entity || TYPES[w.type].name);
    const shown = this._text(w);
    const n = parseFloat(w.type === "slider" || w.type === "arc" ? this._level(w) : shown);
    const isNum = Number.isFinite(n);
    const unit = esc(w.unit);
    const range = w.max > w.min ? w.max - w.min : 100;
    const pct = isNum ? clamp(((n - w.min) / range) * 100, 0, 100) : 0;
    const fill = levelColor(w, n, onc);
    let inner;
    switch (w.type) {
      case "label":
        if (w.bg) el.style.background = w.bg;
        inner = `<div class="t">${ic ? ic + " " : ""}${title}</div><div class="v" style="${fontStyle(w)}">${shown === undefined ? "--" : esc(shown) + (unit ? " " + unit : "")}</div>`; break;
      case "button":
        el.style.background = active ? onc : off;
        if (active && !w.fg) el.style.color = "#0f0f1a";
        inner = `<div class="v" style="${fontStyle(w)}">${ic ? `<span style="font-size:${w.font || (w.label ? 18 : 28)}px;margin-right:${w.label ? 6 : 0}px">${ic}</span>` : ""}${ic && !w.label ? "" : title}</div>`; break;
      case "gauge": case "arc": {
        const size = Math.max(10, Math.min(w.w, w.h) - 34);
        const text = w.type === "arc" ? (isNum ? Math.round(n) + unit : "--") : (isNum ? n.toFixed(1) + unit : "--");
        inner = `<div class="t" style="text-align:center">${title}</div><div class="ring" style="width:${size}px;height:${size}px;background:conic-gradient(from 225deg,${fill} ${pct * 2.7}deg,${off} 0 270deg,transparent 0)"></div><div class="v" style="text-align:center;bottom:auto;top:50%;transform:translateY(-40%)">${text}</div>`; break;
      }
      case "switch":
        inner = `<div class="t" style="${fontStyle(w)}">${title}</div><div class="pill${active ? " on" : ""}" style="background:${active ? onc : off}"></div>`; break;
      case "checkbox":
        inner = `<div class="chk" style="background:${active ? onc : off}">${active ? "✓" : ""}</div><div class="t" style="${fontStyle(w)}">${title}</div>`; break;
      case "led": {
        const d = Math.max(8, Math.min(w.w, w.h) - 6);
        inner = `<i style="width:${d}px;height:${d}px;background:${active ? onc : off};box-shadow:${active ? `0 0 ${d / 3}px ${onc}` : "none"}"></i>`; break;
      }
      case "bar":
        inner = `<div class="t">${title}</div><div class="v" style="top:6px;bottom:auto;left:auto;font-size:14px;color:${COL.muted}">${isNum ? Math.round(n) + unit : "--"}</div><div class="track" style="background:${off}"><i style="width:${pct}%;background:${fill}"></i></div>`; break;
      case "slider":
        inner = `<div class="t">${title}</div><div class="v" style="top:6px;bottom:auto;left:auto;font-size:14px;color:${COL.muted}">${isNum ? Math.round(n) + unit : "--"}</div><div class="strack" style="background:${off}"><i style="width:${pct}%;background:${onc}"></i><b style="left:${pct}%"></b></div>`; break;
      case "dropdown":
        inner = `<div class="t">${title}</div><div class="box" style="background:${off};${fontStyle(w)}"><span>${esc(w.entity ? this._selection(w) : "--")}</span><span>▾</span></div>`; break;
      case "roller":
        inner = `<div class="t">${title}</div><div class="box" style="background:${off};justify-content:center;font-size:${w.font || 18}px"><span>${esc(w.entity ? this._selection(w) : "--")}</span></div>`; break;
      case "clock":
        inner = esc(clockText(w.format)); el.style.fontSize = `${w.font || 30}px`; if (w.bg) el.style.background = w.bg; break;
      case "panel":
        el.style.background = w.bg || COL.card;
        inner = w.label ? `<div class="t">${title}</div>` : ""; break;
      case "media": {
        const t = attrs.media_title || (w.entity ? state || "--" : "--");
        const vol = typeof attrs.volume_level === "number" ? Math.round(attrs.volume_level * 100) : 0;
        if (w.bg) el.style.background = w.bg;
        inner = `<div class="t" style="${fontStyle(w)}">${esc(t)}</div><div class="a">${esc(attrs.media_artist || w.label || "")}</div>
          <div class="ctl" style="top:${Math.round(w.h * 0.38)}px;color:${w.fg || COL.text}"><span>⏮</span><span style="color:${onc}">${state === "playing" ? "⏸" : "▶"}</span><span>⏭</span></div>
          <div class="strack" style="background:${off}"><i style="width:${vol}%;background:${onc}"></i><b style="left:${vol}%"></b></div>`; break;
      }
      case "thermostat": {
        const cur = attrs.current_temperature, tgt = attrs.temperature;
        if (w.bg) el.style.background = w.bg;
        inner = `<div class="t">${title}${state ? " · " + esc(state) : ""}${cur !== undefined ? " · " + esc(cur) + "°" : ""}</div><div class="thermo"><em style="color:${onc}">−</em><big style="${fontStyle(w)}">${tgt !== undefined && tgt !== null ? esc(tgt) + "°" : "--"}</big><em style="color:${onc}">+</em></div>`; break;
      }
      default:
        inner = `<div class="t">${title}</div>`;
    }
    el.innerHTML = inner + (w.id === this._selected ? '<div class="rz"></div>' : "");
  }

  _selection(w) {
    const a = this._hass?.states?.[w.entity]?.attributes || {};
    return w.entity.startsWith("media_player.") ? a.source || "--" : this._hass?.states?.[w.entity]?.state || "--";
  }

  _signature() {
    return `${this._page}|` + this._widgets.map((w) => `${w.id}:${this._state(w)}${this._hass?.states?.[w.entity]?.last_updated ?? ""}${w.type === "clock" ? new Date().getMinutes() : ""}`).join("|");
  }

  _refreshValues() {
    if (!this.shadowRoot.getElementById("canvas")) return;
    const sig = this._signature();
    if (sig !== this._sig) {
      this._sig = sig;
      for (const w of this._items()) {
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
          w.w = clamp(snap(start.ww + dx), w.type === "pager" ? PAGER_MIN_W : MIN_SIZE, this._W - w.x);
          w.h = clamp(snap(start.wh + dy), w.type === "pager" ? PAGER_MIN_H : MIN_SIZE, this._H - w.y);
          this._paint(el, w);
        } else {
          w.x = clamp(snap(start.wx + dx), 0, this._W - w.w);
          w.y = clamp(snap(start.wy + dy), 0, this._H - w.h);
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

  _colorField(key, label, value, fallback) {
    return `<div class="field"><label>${label}</label><div class="color"><input type="color" id="p-${key}" value="${value || fallback}" ${value ? "" : 'class="unset"'}><button class="mini" id="p-${key}-x" ${value ? "" : "disabled"}>Standard</button></div></div>`;
  }

  _renderProps() {
    const panel = this.shadowRoot.getElementById("props");
    if (!panel) return;
    const w = this._find(this._selected);
    if (!w) return this._renderPageProps(panel);
    if (w.type === "pager") return this._renderPagerProps(panel, w);
    const t = w.type, domain = w.entity.split(".")[0];
    this._propsDomain = domain;
    const numeric = ["gauge", "bar", "slider", "arc"].includes(t);
    const levels = t === "gauge" || t === "bar";
    const colors = {
      label: ["Hintergrund", "Text", null], button: ["Farbe (aus)", "Text", "Farbe (an)"], switch: ["Schiene (aus)", "Text", "Schiene (an)"],
      checkbox: ["Kästchen (aus)", "Text", "Kästchen (an)"], led: ["Farbe (aus)", null, "Farbe (an)"], bar: ["Spur", "Text", "Balken"], gauge: ["Spur", "Text", "Anzeige"],
      slider: ["Spur", "Text", "Regler"], arc: ["Spur", "Text", "Regler"], dropdown: ["Feld", "Text", "Hervorhebung"], roller: ["Feld", "Text", "Hervorhebung"],
      clock: ["Hintergrund", "Text", null], panel: ["Fläche", "Text", null], media: ["Hintergrund", "Text", "Akzent"], thermostat: ["Hintergrund", "Text", "Akzent"],
    }[t] || [null, null, null];
    const acts = t === "button" ? ACTIONS[domain] : null;
    const opt = (v, text, cur) => `<option value="${v}" ${cur === v ? "selected" : ""}>${esc(text)}</option>`;
    panel.innerHTML = `<h3>${TYPES[t].name}</h3>
      ${NO_ENTITY(t) ? "" : `<div class="field"><label>Entität</label><input id="p-entity" list="p-ents" value="${esc(w.entity)}" placeholder="sensor.temperatur" autocomplete="off"><datalist id="p-ents">${this._entityIds(t).map((id) => `<option value="${esc(id)}">`).join("")}</datalist></div>`}
      <div class="field"><label>Beschriftung</label><input id="p-label" value="${esc(w.label)}" maxlength="31" placeholder="${esc(this._hass.states[w.entity]?.attributes.friendly_name || "")}"></div>
      ${acts ? `<div class="field"><label>Aktion</label><select id="p-action">${opt("", domain === "climate" ? "Bitte wählen" : domain === "media_player" ? "Standard (Wiedergabe / Pause)" : "Standard (umschalten)", w.action)}${acts.map(([v, n2]) => opt(v, n2, w.action)).join("")}</select></div>` : ""}
      ${t === "button" || t === "label" ? `<div class="field"><label>Symbol</label><select id="p-icon">${opt("", "Kein Symbol", w.icon)}${Object.entries(ICONS).map(([k, [ch, n2]]) => opt(k, `${ch}  ${n2}`, w.icon)).join("")}</select></div>` : ""}
      ${t === "label" || t === "bar" || t === "gauge" || t === "led" ? `<div class="field"><label>Attribut statt Zustand</label><input id="p-attr" value="${esc(w.attr)}" placeholder="z. B. battery" autocomplete="off"></div>` : ""}
      ${["label", "bar", "gauge", "slider", "arc"].includes(t) ? `<div class="field"><label>Einheit</label><input id="p-unit" value="${esc(w.unit)}" maxlength="15" placeholder="°C"></div>` : ""}
      ${numeric ? `<div class="two"><div class="field"><label>Min</label><input id="p-min" type="number" value="${w.min}"></div><div class="field"><label>Max</label><input id="p-max" type="number" value="${w.max}"></div></div>` : ""}
      ${levels ? `<div class="two"><div class="field"><label>Warnung ab</label><input id="p-warn" type="number" step="any" value="${w.warn ?? ""}"></div><div class="field"><label>Kritisch ab</label><input id="p-crit" type="number" step="any" value="${w.crit ?? ""}"></div></div>
        <label class="check"><input type="checkbox" id="p-invert" ${w.invert ? "checked" : ""}> Schwellen gelten für Werte darunter</label>` : ""}
      ${t === "clock" ? `<div class="field"><label>Format</label><select id="p-format">${FORMATS.map(([v, n2]) => opt(v, n2, w.format)).join("")}</select></div>` : ""}
      ${FONT_TYPES.includes(t) ? `<div class="field"><label>Schriftgröße</label><select id="p-font">${opt(0, "Automatisch", w.font)}${[...FONTS, ...(t === "clock" ? [32, 48] : [])].map((f) => opt(f, `${f} px`, w.font)).join("")}</select></div>` : ""}
      ${colors[0] ? this._colorField("bg", colors[0], w.bg, COL.off) : ""}
      ${colors[2] ? this._colorField("on", colors[2], w.on, COL.on) : ""}
      ${colors[1] ? this._colorField("fg", colors[1], w.fg, COL.text) : ""}
      <div class="two"><div class="field"><label>X</label><input id="p-x" type="number" value="${w.x}"></div><div class="field"><label>Y</label><input id="p-y" type="number" value="${w.y}"></div></div>
      <div class="two"><div class="field"><label>Breite</label><input id="p-w" type="number" value="${w.w}"></div><div class="field"><label>Höhe</label><input id="p-h" type="number" value="${w.h}"></div></div>
      <button class="btn danger" id="p-del">Element löschen</button>`;
    const repaint = () => {
      this._setDirty(true);
      const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
      if (el) this._paint(el, w);
    };
    const bind = (id, apply, ev = "input") => panel.querySelector(id)?.addEventListener(ev, (e) => { apply(e.target.value, e.target); repaint(); });
    const [minW, minH] = minSize(t);
    bind("#p-entity", (v) => { w.entity = v.trim(); this._sig = ""; });
    panel.querySelector("#p-entity")?.addEventListener("change", () => this._entityChanged(w));
    bind("#p-label", (v) => { w.label = cutBytes(v, 31); });
    bind("#p-unit", (v) => { w.unit = cutBytes(v, 15); });
    bind("#p-attr", (v) => { w.attr = v.trim().toLowerCase().replace(/[^a-z0-9_]/g, "").slice(0, 23); });
    bind("#p-min", (v) => { w.min = Math.round(num(v, 0)); });
    bind("#p-max", (v) => { w.max = Math.round(num(v, 100)); });
    bind("#p-warn", (v) => { w.warn = v === "" ? null : num(v, null); });
    bind("#p-crit", (v) => { w.crit = v === "" ? null : num(v, null); });
    bind("#p-invert", (v, el) => { w.invert = el.checked; }, "change");
    bind("#p-format", (v) => { w.format = v; }, "change");
    bind("#p-icon", (v) => { w.icon = v; }, "change");
    bind("#p-font", (v) => { w.font = Number(v) || 0; }, "change");
    bind("#p-action", (v) => { w.action = v; if (v && !w.icon && ACTION_ICON[v]) w.icon = ACTION_ICON[v]; this._renderProps(); }, "change");
    for (const key of ["bg", "on", "fg"]) {
      bind(`#p-${key}`, (v) => { w[key] = v; });
      panel.querySelector(`#p-${key}`)?.addEventListener("input", (e) => { e.target.classList.remove("unset"); const x = panel.querySelector(`#p-${key}-x`); if (x) x.disabled = false; });
      panel.querySelector(`#p-${key}-x`)?.addEventListener("click", () => { w[key] = ""; repaint(); this._renderProps(); });
    }
    bind("#p-x", (v) => { w.x = clamp(Math.round(num(v, 0)), 0, this._W - w.w); });
    bind("#p-y", (v) => { w.y = clamp(Math.round(num(v, 0)), 0, this._H - w.h); });
    bind("#p-w", (v) => { w.w = clamp(Math.round(num(v, w.w)), minW, this._W - w.x); });
    bind("#p-h", (v) => { w.h = clamp(Math.round(num(v, w.h)), minH, this._H - w.y); });
    panel.querySelector("#p-del").addEventListener("click", () => this._remove(w.id));
  }

  // After the entity was typed/chosen: fit range and action to what it can do.
  _entityChanged(w) {
    const domain = w.entity.split(".")[0];
    const attrs = this._hass.states[w.entity]?.attributes || {};
    const before = JSON.stringify([w.min, w.max, w.action]);
    if (w.type === "slider" || w.type === "arc") {
      if (domain === "input_number" || domain === "number") {
        w.min = Math.round(num(attrs.min, 0));
        w.max = Math.round(num(attrs.max, 100));
        if (w.max <= w.min) w.max = w.min + 1;
      } else { w.min = 0; w.max = 100; }
    }
    if (w.type === "button") {
      const valid = (ACTIONS[domain] || []).map(([v]) => v);
      if (!valid.includes(w.action)) w.action = domain === "climate" ? "temp_up" : "";
    }
    // Re-render the form only when its fields changed: it would steal the focus of whatever the user clicked next.
    if (before !== JSON.stringify([w.min, w.max, w.action]) || (w.type === "button" && domain !== this._propsDomain)) {
      this._setDirty(true);
      this._renderProps();
    }
    const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
    if (el) this._paint(el, w);
  }

  _renderPageProps(panel) {
    const page = this._pages[this._page];
    panel.innerHTML = `<h3>Seite ${this._page + 1} von ${this._pages.length}</h3>
      <div class="field"><label>Name der Seite</label><input id="pg-name" value="${esc(page.name)}" placeholder="z. B. Wohnzimmer"></div>
      ${this._colorField("pgbg", "Hintergrundfarbe", page.bg, "#0f0f1a")}
      <div class="two"><button class="btn" id="pg-left" ${this._page === 0 ? "disabled" : ""}>◀ Nach vorn</button><button class="btn" id="pg-right" ${this._page === this._pages.length - 1 ? "disabled" : ""}>Nach hinten ▶</button></div>
      <div class="two" style="margin-top:6px"><button class="btn" id="pg-dup" ${this._pages.length >= MAX_PAGES ? "disabled" : ""}>Duplizieren</button><button class="btn danger" id="pg-del" ${this._pages.length < 2 ? "disabled" : ""}>Löschen</button></div>
      <div class="empty" style="margin-top:12px">${this._widgets.length} von ${MAX_WIDGETS} Elementen auf dieser Seite.<br>Element aus der Palette ziehen oder anklicken, dann Entität und Beschriftung festlegen.<br>Pfeiltasten verschieben, Entf löscht.<br><br>Mit dem <b>Seitenwechsel</b> aus der Palette bekommt das Display Pfeile zum Blättern. Position und Größe bestimmst du frei; er gilt für alle Seiten.</div>`;
    panel.querySelector("#pg-name").addEventListener("input", (e) => {
      e.target.value = cutBytes(e.target.value, 31);
      page.name = e.target.value;
      this._setDirty(true);
      this._renderPages();
      const el = this.shadowRoot.querySelector(`.w[data-id="${PAGER_ID}"]`);
      if (el) this._paint(el, this._pager);
    });
    const bgInput = panel.querySelector("#p-pgbg"), bgReset = panel.querySelector("#p-pgbg-x");
    bgInput.addEventListener("input", (e) => {
      page.bg = e.target.value;
      e.target.classList.remove("unset");
      bgReset.disabled = false;
      this._setDirty(true);
      this.shadowRoot.getElementById("screen").style.background = page.bg;
    });
    bgReset.addEventListener("click", () => { page.bg = ""; this._setDirty(true); this._draw(); this._renderProps(); });
    panel.querySelector("#pg-left").addEventListener("click", () => this._movePage(-1));
    panel.querySelector("#pg-right").addEventListener("click", () => this._movePage(1));
    panel.querySelector("#pg-dup").addEventListener("click", () => this._addPage(true));
    panel.querySelector("#pg-del").addEventListener("click", () => this._deletePage());
  }

  _renderPagerProps(panel, w) {
    panel.innerHTML = `<h3>Seitenwechsel</h3>
      <div class="field"><label>Anzeige in der Mitte</label><select id="p-mode"><option value="name" ${w.mode === "name" ? "selected" : ""}>Name der Seite</option><option value="count" ${w.mode === "count" ? "selected" : ""}>Nummer (2 / 3)</option></select></div>
      <div class="two"><div class="field"><label>X</label><input id="p-x" type="number" value="${w.x}"></div><div class="field"><label>Y</label><input id="p-y" type="number" value="${w.y}"></div></div>
      <div class="two"><div class="field"><label>Breite</label><input id="p-w" type="number" value="${w.w}"></div><div class="field"><label>Höhe</label><input id="p-h" type="number" value="${w.h}"></div></div>
      <div class="empty" style="margin-bottom:10px">Links und rechts tippt man zum Blättern. Das Element liegt auf allen Seiten an derselben Stelle und erscheint am Display erst, wenn es mindestens 2 Seiten gibt.</div>
      <button class="btn danger" id="p-del">Entfernen</button>`;
    const bind = (id, apply) => panel.querySelector(id)?.addEventListener("input", (e) => {
      apply(e.target.value);
      this._setDirty(true);
      const el = this.shadowRoot.querySelector(`.w[data-id="${PAGER_ID}"]`);
      if (el) this._paint(el, w);
    });
    bind("#p-mode", (v) => { w.mode = v === "count" ? "count" : "name"; });
    bind("#p-x", (v) => { w.x = clamp(Math.round(num(v, 0)), 0, this._W - w.w); });
    bind("#p-y", (v) => { w.y = clamp(Math.round(num(v, 0)), 0, this._H - w.h); });
    bind("#p-w", (v) => { w.w = clamp(Math.round(num(v, w.w)), PAGER_MIN_W, this._W - w.x); });
    bind("#p-h", (v) => { w.h = clamp(Math.round(num(v, w.h)), PAGER_MIN_H, this._H - w.y); });
    panel.querySelector("#p-del").addEventListener("click", () => this._remove(PAGER_ID));
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

function register() {
  customElements.define("display-card", DisplayCard);
  customElements.define("display-card-editor", DisplayCardEditor);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "display-card",
    name: "ESP32 Display",
    description: `Layout-Editor für ESP32-Display-Panels (v${CARD_VERSION})`,
  });
}

// This file is loaded very early as an "extra module". Home Assistant installs its own customElements registry
// a moment later, and anything defined before that is invisible to it ("Custom element not found" in the card
// picker, an endless spinner). So inside Home Assistant, wait until its app has defined itself.
if (document.querySelector("home-assistant")) {
  const started = Date.now();
  const wait = () => {
    if (customElements.get("home-assistant") || Date.now() - started > 30000) register();
    else setTimeout(wait, 50);
  };
  wait();
} else {
  register();
}
