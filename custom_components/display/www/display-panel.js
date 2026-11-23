// Sidebar panel: the layout editor (display-card) on a page of its own. Registered by the integration (admins only).
class DisplayPanel extends HTMLElement {
  set hass(hass) {
    this._hass = hass;
    if (this._card) this._card.hass = hass;
    if (this._menu) this._menu.hass = hass;
  }
  set narrow(narrow) {
    this._narrow = narrow;
    if (this._menu) this._menu.narrow = narrow;
  }
  set panel(_) {}
  set route(_) {}

  connectedCallback() {
    if (this._ready) return;
    this._ready = true;
    this.style.cssText = "display:block;min-height:100vh;background:var(--primary-background-color);";
    const head = document.createElement("div");
    head.style.cssText = "display:flex;align-items:center;gap:8px;padding:8px 12px;height:56px;box-sizing:border-box;background:var(--app-header-background-color,var(--primary-color));color:var(--app-header-text-color,#fff);font-size:20px;";
    this._menu = document.createElement("ha-menu-button");
    this._menu.hass = this._hass;
    this._menu.narrow = this._narrow;
    head.append(this._menu, Object.assign(document.createElement("span"), { textContent: "ESP32 Display" }));
    const body = document.createElement("div");
    body.style.cssText = "max-width:1400px;margin:0 auto;padding:16px;";
    this.append(head, body);
    customElements.whenDefined("display-card").then(() => {
      this._card = document.createElement("display-card");
      this._card.setConfig({});
      if (this._hass) this._card.hass = this._hass;
      body.append(this._card);
    });
  }
}
customElements.define("display-panel", DisplayPanel);
