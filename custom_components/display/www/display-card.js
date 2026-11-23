// ESP32 Display Card — Editor
// TODO: implement visual layout editor

class DisplayCard extends HTMLElement {
    set hass(hass) {
        this._hass = hass;
    }

    setConfig(config) {
        this._config = config;
        if (!this.shadowRoot) {
            this.attachShadow({ mode: "open" });
        }
        this.shadowRoot.innerHTML = `
            <style>
                :host { display: block; padding: 16px; }
                .placeholder { text-align: center; color: var(--secondary-text-color); padding: 32px; }
            </style>
            <div class="placeholder">ESP32 Display Editor — coming soon</div>
        `;
    }

    getCardSize() { return 3; }
}

customElements.define("display-card", DisplayCard);
window.customCards = window.customCards || [];
window.customCards.push({
    type: "display-card",
    name: "ESP32 Display",
    description: "Visual editor for ESP32 display panels",
});
