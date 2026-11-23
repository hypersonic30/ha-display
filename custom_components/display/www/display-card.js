// ESP32 Display Card — Visual Layout Editor
// Communicates with /api/display/layout/{device} and /api/display/devices

const CARD_VERSION = "0.1.0";

const WIDGET_TYPES = ["label", "button", "gauge", "switch", "bar"];
const WIDGET_DEFAULTS = {
  label:  { w: 180, h: 70,  label: "", unit: "" },
  button: { w: 130, h: 80,  label: "", service_domain: "homeassistant" },
  gauge:  { w: 150, h: 150, label: "", unit: "", min: 0, max: 100 },
  switch: { w: 170, h: 60,  label: "", service_domain: "homeassistant" },
  bar:    { w: 210, h: 60,  label: "", unit: "%", min: 0, max: 100 },
};

// Display physical resolution
const DW = 480, DH = 320;

// ── Styles ────────────────────────────────────────────────────────────────────

const STYLE = `
  :host { display: block; font-family: var(--paper-font-body1_-_font-family, sans-serif); }
  * { box-sizing: border-box; }

  .root { background: var(--card-background-color); border-radius: 12px; overflow: hidden; }

  /* Toolbar */
  .toolbar {
    display: flex; align-items: center; gap: 10px; padding: 12px 16px;
    border-bottom: 1px solid var(--divider-color);
  }
  .toolbar select {
    flex: 1; background: var(--secondary-background-color);
    border: 1px solid var(--divider-color); border-radius: 8px;
    color: var(--primary-text-color); padding: 6px 10px; font-size: 14px;
  }
  .btn {
    border: none; border-radius: 8px; padding: 7px 14px; cursor: pointer;
    font-size: 13px; font-weight: 500;
  }
  .btn-primary { background: var(--primary-color); color: #fff; }
  .btn-secondary { background: var(--secondary-background-color); color: var(--primary-text-color); }
  .btn-danger { background: #ef5350; color: #fff; }
  .btn:disabled { opacity: .45; cursor: not-allowed; }

  /* Layout */
  .body { display: flex; gap: 0; }

  /* Palette */
  .palette {
    width: 100px; padding: 10px 8px; border-right: 1px solid var(--divider-color);
    display: flex; flex-direction: column; gap: 6px; flex-shrink: 0;
  }
  .palette-item {
    background: var(--secondary-background-color); border: 1px solid var(--divider-color);
    border-radius: 8px; padding: 7px 4px; text-align: center; cursor: grab;
    font-size: 12px; color: var(--primary-text-color); user-select: none;
  }
  .palette-item:active { cursor: grabbing; opacity: .7; }
  .palette-label { font-size: 11px; color: var(--secondary-text-color); margin-top: 2px; }

  /* Canvas area */
  .canvas-wrap {
    flex: 1; display: flex; align-items: flex-start; justify-content: center;
    padding: 16px; overflow: auto; background: var(--secondary-background-color);
  }
  .canvas {
    position: relative; background: #0f0f1a; border-radius: 8px;
    box-shadow: 0 4px 20px rgba(0,0,0,.4); flex-shrink: 0;
  }
  .canvas-inner { position: absolute; inset: 0; overflow: hidden; }

  /* Widgets on canvas */
  .w {
    position: absolute; background: #1a1a2e; border-radius: 8px;
    border: 2px solid transparent; cursor: grab; overflow: hidden;
    display: flex; flex-direction: column; padding: 6px 8px;
  }
  .w:hover { border-color: rgba(79,195,247,.4); }
  .w.selected { border-color: #4fc3f7 !important; }
  .w:active { cursor: grabbing; }
  .w-title { font-size: 10px; color: #778899; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .w-value { font-size: 16px; color: #e0e0e0; margin-top: auto; }
  .w-type  { font-size: 9px; color: #4fc3f7; text-transform: uppercase; letter-spacing: .5px; }

  /* Button preview */
  .w.type-button { background: #2a2a44; align-items: center; justify-content: center; }
  .w.type-button .w-value { font-size: 13px; text-align: center; margin: 0; }

  /* Gauge preview */
  .w.type-gauge { align-items: center; justify-content: center; }
  .w-gauge-arc {
    width: 70%; aspect-ratio: 1; border-radius: 50%;
    border: 5px solid #2a2a44; border-top-color: #4fc3f7; border-right-color: #4fc3f7;
    margin: 4px auto;
  }

  /* Switch preview */
  .w-switch-row { display: flex; justify-content: space-between; align-items: center; margin-top: auto; }
  .w-switch-pill {
    width: 36px; height: 20px; background: #2a2a44; border-radius: 10px; position: relative;
  }
  .w-switch-pill::after {
    content: ''; position: absolute; width: 14px; height: 14px; background: #778899;
    border-radius: 50%; top: 3px; left: 3px; transition: .2s;
  }

  /* Bar preview */
  .w-bar-track { height: 8px; background: #2a2a44; border-radius: 4px; margin-top: auto; }
  .w-bar-fill  { height: 100%; background: #4fc3f7; border-radius: 4px; width: 50%; }

  /* Properties panel */
  .props {
    width: 220px; padding: 14px; border-left: 1px solid var(--divider-color);
    overflow-y: auto; flex-shrink: 0;
  }
  .props-empty { color: var(--secondary-text-color); font-size: 13px; text-align: center; padding-top: 24px; }
  .props h3 { margin: 0 0 12px; font-size: 13px; color: var(--primary-text-color); }
  .field { margin-bottom: 10px; }
  .field label { display: block; font-size: 11px; color: var(--secondary-text-color); margin-bottom: 3px; }
  .field input, .field select {
    width: 100%; background: var(--secondary-background-color);
    border: 1px solid var(--divider-color); border-radius: 6px;
    color: var(--primary-text-color); padding: 6px 8px; font-size: 13px;
  }
  .field-row { display: flex; gap: 6px; }
  .field-row .field { flex: 1; }
  .props-actions { margin-top: 14px; display: flex; flex-direction: column; gap: 6px; }

  .toast {
    position: fixed; bottom: 24px; left: 50%; transform: translateX(-50%);
    background: #333; color: #fff; padding: 10px 20px; border-radius: 8px;
    font-size: 13px; z-index: 9999; opacity: 0; transition: opacity .3s;
    pointer-events: none;
  }
  .toast.show { opacity: 1; }
`;

// ── Widget preview HTML ───────────────────────────────────────────────────────

function widgetPreviewHtml(w) {
  const title = w.label || w.entity || w.type;
  switch (w.type) {
    case "button":
      return `<div class="w-title w-type">${w.type}</div><div class="w-value">${title}</div>`;
    case "gauge":
      return `<div class="w-title">${title}</div><div class="w-gauge-arc"></div><div class="w-value" style="font-size:12px">--${w.unit||""}</div>`;
    case "switch":
      return `<div class="w-title">${title}</div><div class="w-switch-row"><span class="w-value" style="font-size:12px">--</span><div class="w-switch-pill"></div></div>`;
    case "bar":
      return `<div class="w-title">${title}</div><span class="w-value" style="font-size:12px">-- ${w.unit||""}</span><div class="w-bar-track"><div class="w-bar-fill"></div></div>`;
    default: // label
      return `<div class="w-title">${title}</div><div class="w-value">-- ${w.unit||""}</div>`;
  }
}

// ── Main card class ───────────────────────────────────────────────────────────

class DisplayCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._hass    = null;
    this._config  = {};
    this._layout  = { widgets: [] };
    this._device  = "";
    this._devices = [];
    this._selected = null;   // widget id
    this._scale   = 1;
    this._idSeq   = 0;
    this._dirty   = false;
  }

  set hass(h) {
    this._hass = h;
    if (!this._rendered) this._render();
  }

  setConfig(config) {
    this._config = config || {};
    this._device = config.device || "";
  }

  getCardSize() { return 7; }

  // ── Render shell ────────────────────────────────────────────────────────────

  _render() {
    this._rendered = true;
    this.shadowRoot.innerHTML = `
      <style>${STYLE}</style>
      <div class="root">
        <div class="toolbar">
          <select id="dev-sel"></select>
          <button class="btn btn-secondary" id="btn-refresh" title="Neu laden">↻</button>
          <button class="btn btn-primary" id="btn-save" disabled>Speichern</button>
        </div>
        <div class="body">
          <div class="palette">
            <div style="font-size:11px;color:var(--secondary-text-color);margin-bottom:4px">Widgets</div>
            ${WIDGET_TYPES.map(t => `
              <div class="palette-item" draggable="true" data-wtype="${t}" title="${t} hinzufügen">
                ${this._typeIcon(t)}
                <div class="palette-label">${t}</div>
              </div>`).join("")}
          </div>
          <div class="canvas-wrap" id="canvas-wrap">
            <div class="canvas" id="canvas">
              <div class="canvas-inner" id="canvas-inner"></div>
            </div>
          </div>
          <div class="props" id="props">
            <div class="props-empty">Widget auswählen oder aus der Palette ziehen</div>
          </div>
        </div>
      </div>
      <div class="toast" id="toast"></div>`;

    this._setupCanvas();
    this._setupToolbar();
    this._setupPalette();
    this._loadDevices();
  }

  _typeIcon(t) {
    return { label:"Aa", button:"⬛", gauge:"◉", switch:"⇌", bar:"▬" }[t] || "?";
  }

  // ── Canvas scaling ──────────────────────────────────────────────────────────

  _setupCanvas() {
    const wrap  = this.shadowRoot.getElementById("canvas-wrap");
    const canvas = this.shadowRoot.getElementById("canvas");

    const resize = () => {
      const avail = wrap.clientWidth - 32;
      this._scale = Math.min(1, avail / DW);
      canvas.style.width  = (DW * this._scale) + "px";
      canvas.style.height = (DH * this._scale) + "px";
      const inner = this.shadowRoot.getElementById("canvas-inner");
      inner.style.transform       = `scale(${this._scale})`;
      inner.style.transformOrigin = "top left";
      inner.style.width  = DW + "px";
      inner.style.height = DH + "px";
    };
    new ResizeObserver(resize).observe(wrap);
    setTimeout(resize, 50);

    // Click on canvas background → deselect
    this.shadowRoot.getElementById("canvas-inner").addEventListener("click", e => {
      if (e.target === e.currentTarget) this._select(null);
    });

    // Drop from palette onto canvas
    const inner = this.shadowRoot.getElementById("canvas-inner");
    inner.addEventListener("dragover", e => e.preventDefault());
    inner.addEventListener("drop", e => {
      e.preventDefault();
      const type = e.dataTransfer.getData("wtype");
      if (!type) return;
      const rect = inner.getBoundingClientRect();
      const x = Math.round((e.clientX - rect.left) / this._scale);
      const y = Math.round((e.clientY - rect.top)  / this._scale);
      this._addWidget(type, x, y);
    });
  }

  // ── Toolbar ─────────────────────────────────────────────────────────────────

  _setupToolbar() {
    this.shadowRoot.getElementById("btn-save").addEventListener("click", () => this._save());
    this.shadowRoot.getElementById("btn-refresh").addEventListener("click", () => {
      if (this._device) this._loadLayout(this._device);
    });
    this.shadowRoot.getElementById("dev-sel").addEventListener("change", e => {
      this._device = e.target.value;
      this._loadLayout(this._device);
    });
  }

  // ── Palette drag ────────────────────────────────────────────────────────────

  _setupPalette() {
    this.shadowRoot.querySelectorAll(".palette-item").forEach(el => {
      el.addEventListener("dragstart", e => {
        e.dataTransfer.setData("wtype", el.dataset.wtype);
      });
      // Also click to add at center
      el.addEventListener("click", () => {
        this._addWidget(el.dataset.wtype, Math.round(DW / 2 - 60), Math.round(DH / 2 - 40));
      });
    });
  }

  // ── API calls ────────────────────────────────────────────────────────────────

  _apiBase() { return `/api/display`; }

  async _fetch(path, opts = {}) {
    const r = await fetch(this._apiBase() + path, {
      ...opts,
      headers: {
        "Authorization": `Bearer ${this._hass.auth.data.access_token}`,
        "Content-Type": "application/json",
        ...(opts.headers || {})
      }
    });
    return r;
  }

  async _loadDevices() {
    try {
      const r = await this._fetch("/devices");
      const j = await r.json();
      this._devices = j.devices || [];
      // Also add device from config if set
      if (this._device && !this._devices.includes(this._device))
        this._devices.unshift(this._device);
      this._renderDeviceSelect();
      if (this._device) this._loadLayout(this._device);
    } catch (e) {
      this._toast("Geräte konnten nicht geladen werden");
    }
  }

  _renderDeviceSelect() {
    const sel = this.shadowRoot.getElementById("dev-sel");
    if (!this._devices.length) {
      sel.innerHTML = `<option value="">Noch kein Layout gespeichert</option>`;
      return;
    }
    sel.innerHTML = this._devices
      .map(d => `<option value="${esc(d)}" ${d === this._device ? "selected" : ""}>${esc(d)}</option>`)
      .join("");
  }

  async _loadLayout(device) {
    try {
      const r = await this._fetch(`/layout/${encodeURIComponent(device)}`);
      if (r.status === 404) {
        this._layout = { device, widgets: [] };
      } else {
        this._layout = await r.json();
      }
      this._idSeq  = 0;
      this._selected = null;
      this._dirty  = false;
      this._buildCanvas();
      this._renderProps(null);
      this._setSaveDirty(false);
    } catch (e) {
      this._toast("Layout konnte nicht geladen werden");
    }
  }

  async _save() {
    if (!this._device) {
      this._toast("Kein Gerät ausgewählt");
      return;
    }
    const btn = this.shadowRoot.getElementById("btn-save");
    btn.disabled = true;
    btn.textContent = "…";
    try {
      const r = await this._fetch(`/layout/${encodeURIComponent(this._device)}`, {
        method: "POST",
        body: JSON.stringify({ widgets: this._layout.widgets })
      });
      if (r.ok) {
        this._dirty = false;
        this._setSaveDirty(false);
        this._toast("Gespeichert ✓");
        // Add to device list if new
        if (!this._devices.includes(this._device)) {
          this._devices.push(this._device);
          this._renderDeviceSelect();
        }
      } else {
        const j = await r.json();
        this._toast("Fehler: " + (j.message || r.status));
      }
    } catch (e) {
      this._toast("Speichern fehlgeschlagen");
    }
    btn.disabled = false;
    btn.textContent = "Speichern";
  }

  // ── Canvas rendering ─────────────────────────────────────────────────────────

  _buildCanvas() {
    const inner = this.shadowRoot.getElementById("canvas-inner");
    inner.innerHTML = "";
    this._layout.widgets.forEach(w => this._renderWidget(w));
  }

  _renderWidget(w) {
    const inner = this.shadowRoot.getElementById("canvas-inner");
    const el = document.createElement("div");
    el.className = `w type-${w.type}${w.id === this._selected ? " selected" : ""}`;
    el.dataset.id = w.id;
    el.style.left   = w.x + "px";
    el.style.top    = w.y + "px";
    el.style.width  = w.w + "px";
    el.style.height = w.h + "px";
    el.innerHTML = widgetPreviewHtml(w);

    // Select on click
    el.addEventListener("click", e => { e.stopPropagation(); this._select(w.id); });

    // Drag to move
    this._makeDraggable(el, w);

    inner.appendChild(el);
  }

  _makeDraggable(el, w) {
    let sx, sy, ox, oy;
    el.addEventListener("pointerdown", e => {
      if (e.button !== 0) return;
      sx = e.clientX; sy = e.clientY;
      ox = w.x; oy = w.y;
      el.setPointerCapture(e.pointerId);
      e.stopPropagation();
    });
    el.addEventListener("pointermove", e => {
      if (!e.buttons) return;
      const dx = Math.round((e.clientX - sx) / this._scale);
      const dy = Math.round((e.clientY - sy) / this._scale);
      w.x = Math.max(0, Math.min(DW - w.w, ox + dx));
      w.y = Math.max(0, Math.min(DH - w.h, oy + dy));
      el.style.left = w.x + "px";
      el.style.top  = w.y + "px";
      this._setSaveDirty(true);
    });
    el.addEventListener("pointerup", () => {
      this._updatePropsPosition(w);
    });
  }

  // ── Selection & properties panel ────────────────────────────────────────────

  _select(id) {
    this._selected = id;
    // Update border on all widgets
    this.shadowRoot.querySelectorAll(".w").forEach(el => {
      el.classList.toggle("selected", el.dataset.id === id);
    });
    const w = id ? this._layout.widgets.find(x => x.id === id) : null;
    this._renderProps(w);
  }

  _renderProps(w) {
    const panel = this.shadowRoot.getElementById("props");
    if (!w) {
      panel.innerHTML = `<div class="props-empty">Widget auswählen<br>oder aus der Palette ziehen</div>`;
      return;
    }

    const entityOptions = this._entityOptions(w.type);

    panel.innerHTML = `
      <h3>${w.type.charAt(0).toUpperCase() + w.type.slice(1)}</h3>
      <div class="field">
        <label>Entität</label>
        <select id="p-entity">${entityOptions}</select>
      </div>
      <div class="field">
        <label>Bezeichnung</label>
        <input id="p-label" type="text" value="${esc(w.label||"")}" placeholder="z.B. Temperatur">
      </div>
      ${["label","gauge","bar"].includes(w.type) ? `
        <div class="field">
          <label>Einheit</label>
          <input id="p-unit" type="text" value="${esc(w.unit||"")}" placeholder="z.B. °C">
        </div>` : ""}
      ${["gauge","bar"].includes(w.type) ? `
        <div class="field-row">
          <div class="field"><label>Min</label><input id="p-min" type="number" value="${w.min||0}"></div>
          <div class="field"><label>Max</label><input id="p-max" type="number" value="${w.max||100}"></div>
        </div>` : ""}
      ${["button","switch"].includes(w.type) ? `
        <div class="field">
          <label>Service-Domain</label>
          <input id="p-svc" type="text" value="${esc(w.service_domain||"homeassistant")}" placeholder="light / switch / …">
        </div>` : ""}
      <div class="field-row">
        <div class="field"><label>X</label><input id="p-x" type="number" value="${w.x}"></div>
        <div class="field"><label>Y</label><input id="p-y" type="number" value="${w.y}"></div>
      </div>
      <div class="field-row">
        <div class="field"><label>Breite</label><input id="p-w" type="number" value="${w.w}"></div>
        <div class="field"><label>Höhe</label><input id="p-h" type="number" value="${w.h}"></div>
      </div>
      <div class="props-actions">
        <button class="btn btn-danger" id="p-del">Widget löschen</button>
      </div>`;

    // Set current entity selection
    const sel = panel.querySelector("#p-entity");
    if (sel && w.entity) sel.value = w.entity;

    // Wire up change listeners
    const fields = {
      "#p-entity": v => { w.entity = v; this._refreshWidget(w); },
      "#p-label":  v => { w.label = v;  this._refreshWidget(w); },
      "#p-unit":   v => { w.unit = v;   this._refreshWidget(w); },
      "#p-min":    v => { w.min = +v; },
      "#p-max":    v => { w.max = +v; },
      "#p-svc":    v => { w.service_domain = v; },
      "#p-x":      v => { w.x = +v; this._moveWidget(w); },
      "#p-y":      v => { w.y = +v; this._moveWidget(w); },
      "#p-w":      v => { w.w = +v; this._resizeWidget(w); },
      "#p-h":      v => { w.h = +v; this._resizeWidget(w); },
    };
    for (const [sel, fn] of Object.entries(fields)) {
      const el = panel.querySelector(sel);
      if (el) el.addEventListener("input", e => { fn(e.target.value); this._setSaveDirty(true); });
    }
    panel.querySelector("#p-del")?.addEventListener("click", () => this._deleteWidget(w.id));
  }

  _entityOptions(type) {
    if (!this._hass) return "";
    const domainMap = {
      button: ["light","switch","input_boolean","automation","script"],
      switch: ["light","switch","input_boolean"],
      gauge:  ["sensor","input_number"],
      bar:    ["sensor","input_number"],
      label:  null, // all
    };
    const allowed = domainMap[type];
    return Object.keys(this._hass.states)
      .filter(id => !allowed || allowed.some(d => id.startsWith(d + ".")))
      .sort()
      .map(id => `<option value="${esc(id)}">${esc(id)}</option>`)
      .join("");
  }

  _updatePropsPosition(w) {
    const px = this.shadowRoot.getElementById("p-x");
    const py = this.shadowRoot.getElementById("p-y");
    if (px) px.value = w.x;
    if (py) py.value = w.y;
  }

  _refreshWidget(w) {
    const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
    if (el) el.innerHTML = widgetPreviewHtml(w);
  }

  _moveWidget(w) {
    const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
    if (el) { el.style.left = w.x + "px"; el.style.top = w.y + "px"; }
  }

  _resizeWidget(w) {
    const el = this.shadowRoot.querySelector(`.w[data-id="${w.id}"]`);
    if (el) { el.style.width = w.w + "px"; el.style.height = w.h + "px"; }
  }

  // ── Widget add/delete ────────────────────────────────────────────────────────

  _addWidget(type, x, y) {
    const defaults = WIDGET_DEFAULTS[type] || {};
    const w = {
      id: `w${++this._idSeq}`,
      type,
      entity: "",
      x: Math.max(0, Math.min(DW - (defaults.w || 120), x - Math.round((defaults.w || 120) / 2))),
      y: Math.max(0, Math.min(DH - (defaults.h || 70),  y - Math.round((defaults.h || 70) / 2))),
      ...defaults,
    };
    this._layout.widgets.push(w);
    this._renderWidget(w);
    this._select(w.id);
    this._setSaveDirty(true);
  }

  _deleteWidget(id) {
    this._layout.widgets = this._layout.widgets.filter(w => w.id !== id);
    this.shadowRoot.querySelector(`.w[data-id="${id}"]`)?.remove();
    this._select(null);
    this._setSaveDirty(true);
  }

  // ── UI helpers ───────────────────────────────────────────────────────────────

  _setSaveDirty(dirty) {
    this._dirty = dirty;
    const btn = this.shadowRoot.getElementById("btn-save");
    if (btn) {
      btn.disabled = !dirty;
      btn.textContent = dirty ? "Speichern *" : "Speichern";
    }
  }

  _toast(msg, ms = 2500) {
    const el = this.shadowRoot.getElementById("toast");
    if (!el) return;
    el.textContent = msg;
    el.classList.add("show");
    setTimeout(() => el.classList.remove("show"), ms);
  }
}

// ── Editor config (Lovelace card picker) ─────────────────────────────────────

class DisplayCardEditor extends HTMLElement {
  set hass(h) { this._hass = h; }
  setConfig(c) {
    this._config = c;
    this.innerHTML = `
      <div style="padding:16px;font-family:var(--paper-font-body1_-_font-family,sans-serif)">
        <label style="font-size:12px;color:var(--secondary-text-color)">Gerätename (muss mit dem ESP32 übereinstimmen)</label><br>
        <input id="dev" style="width:100%;margin-top:4px;padding:8px;border:1px solid var(--divider-color);border-radius:6px;background:var(--secondary-background-color);color:var(--primary-text-color)"
               value="${esc(c.device||"")}" placeholder="z.B. Küche">
      </div>`;
    this.querySelector("#dev").addEventListener("input", e => {
      this.dispatchEvent(new CustomEvent("config-changed", {
        detail: { config: { ...this._config, device: e.target.value } }, bubbles: true
      }));
    });
  }
}

function esc(s) {
  return String(s || "").replace(/[&<>"']/g, c =>
    ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c]));
}

customElements.define("display-card", DisplayCard);
customElements.define("display-card-editor", DisplayCardEditor);

window.customCards = window.customCards || [];
window.customCards.push({
  type: "display-card",
  name: "ESP32 Display",
  description: `Visueller Editor für ESP32 Display-Panels (v${CARD_VERSION})`,
  preview: false,
  editor: "display-card-editor",
});
