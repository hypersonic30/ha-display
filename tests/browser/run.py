"""Browser tests for display-card.js: a static server plus a stubbed `hass` (callWS) in real Chrome.

    pip install playwright && python tests/browser/run.py
"""
import http.server, json, os, sys, threading
from playwright.sync_api import sync_playwright

WWW = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "custom_components", "display", "www"))
PORT = 8330
PAGE = """<!doctype html><meta charset=utf-8><body><script type="module" src="/display-card.js"></script>
<script>
window.__calls = [];
window.__fetches = [];
window.__fetchResult = { ok: true, status: 200, body: {} };
window.__stub = {};
window.mount = (config, states) => {
  const hass = { states, callService: async (d, sv, data) => { window.__calls.push({ type: "service", domain: d, service: sv, data }); if (window.__serviceError) throw new Error(window.__serviceError); }, callWS: async (msg) => {
      window.__calls.push(JSON.parse(JSON.stringify(msg)));
      const h = window.__stub[msg.type];
      if (!h) throw { code: "not_found", message: "no stub for " + msg.type };
      return typeof h === "function" ? h(msg) : h;
  } };
  hass.fetchWithAuth = async (url, opts) => { window.__fetches.push({ url, method: opts.method, size: opts.body.size, name: opts.body.name }); const r = window.__fetchResult; return { ok: r.ok, status: r.status, json: async () => r.body }; };
  window.hass = hass;
  const card = document.createElement("display-card");
  card.setConfig(config);
  document.body.innerHTML = "";
  document.body.appendChild(card);
  card.hass = hass;
  window.card = card;
};
</script>"""

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=WWW, **k)
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path == "/ha-sim.html":
            body = b'<!doctype html><meta charset=utf-8><body><home-assistant></home-assistant><script type="module" src="/display-card.js"></script>'
            self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        elif self.path == "/test.html":
            body = PAGE.encode(); self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        else: super().do_GET()

threading.Thread(target=http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever, daemon=True).start()

failed = []
def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"  -> {detail}"))
    if not ok: failed.append(name)

STATES = {
    "sensor.temp": {"entity_id": "sensor.temp", "state": "21.5", "attributes": {"friendly_name": "Temperatur"}},
    "light.kitchen": {"entity_id": "light.kitchen", "state": "on", "attributes": {}},
    "sensor.humidity": {"entity_id": "sensor.humidity", "state": "40", "attributes": {}},
    "binary_sensor.kueche_verbunden": {"entity_id": "binary_sensor.kueche_verbunden", "state": "on", "attributes": {"device_class": "connectivity"}},
}
W1 = {"id": "w1", "type": "label", "entity": "sensor.temp", "label": "Temp", "unit": "°C", "min": 0, "max": 100, "x": 10, "y": 10, "w": 150, "h": 70}
DEVICES = {"devices": [{"id": "D1", "name": "Küche", "online": False, "update_entity": "update.kuche_firmware", "connected_entity": "binary_sensor.kueche_verbunden"}, {"id": "D2", "name": "Bad", "online": False, "update_entity": None, "connected_entity": None}]}

def layouts(extra=None):
    store = {"D1": [dict(W1)], "D2": []}
    def get(msg): return {"name": "x", "rev": 1, "widgets": store[msg["device_id"]], "settings": {"brightness": 80}}
    def put(msg):
        store[msg["device_id"]] = msg["widgets"]; return {"rev": 2, "widgets": msg["widgets"]}
    return {"display/devices": DEVICES, "display/layout/get": get, "display/layout/set": put, **(extra or {})}

sh = lambda pg, js: pg.evaluate("() => { const r = card.shadowRoot; return " + js + " }")
calls = lambda pg, t: pg.evaluate("(t) => window.__calls.filter(c => c.type === t)", t)

def open_card(b, stub, config=None, states=STATES):
    pg = b.new_page(viewport={"width": 1100, "height": 900})
    pg.add_init_script("window.confirm = () => (window.__confirm ?? true)")
    errors = []; pg.on("pageerror", lambda e: errors.append(str(e)))
    pg.goto(f"http://127.0.0.1:{PORT}/test.html"); pg.wait_for_function("() => customElements.get('display-card')")
    pg.evaluate("(s) => { window.__stub = s }", None)  # placeholder, replaced below
    return pg, errors

def mount(pg, stub, config=None, states=STATES):
    # functions cannot cross the bridge: the stub is rebuilt in the page from JSON descriptions
    pg.evaluate("""([stub, config, states]) => {
      const store = stub.store;
      window.__stub = {
        "display/devices": () => stub.devices,
        "display/layout/get": (m) => ({ name: "x", rev: 1, ...store[m.device_id], screen: stub.screens[m.device_id] || { w: 480, h: 320 }, settings: { brightness: 80, rotation: 0, click: !!(stub.click || {})[m.device_id] } }),
        "display/settings/set": (m) => { if (stub.settingsError) throw { code: "unauthorized", message: "Unauthorized" }; (stub.click = stub.click || {})[m.device_id] = m.click; return { settings: { click: m.click } }; },
        "display/layout/set": (m) => { if (stub.saveError) throw { code: "invalid_format", message: stub.saveError }; store[m.device_id] = { pages: m.pages, pager: m.pager }; return { rev: 2, name: "x", pages: m.pages, pager: m.pager, screen: stub.screens[m.device_id] || { w: 480, h: 320 }, settings: { brightness: 80, rotation: 0 } }; },
        "display/custom_firmware/get": () => window.__custom || null,
        "display/custom_firmware/discard": () => { window.__custom = null; return null; },
        "display/custom_firmware/install": () => { if (window.__installError) throw { code: "unknown_error", message: window.__installError }; window.__custom = null; return { fw: "0.9.9-test" }; },
        "display/setup_info": (m) => { if (stub.notAdmin) throw { code: "unauthorized", message: "Unauthorized" }; return { name: "Küche", ha_url: "http://ha:8123", device_key: "SECRET-KEY_1" }; },
      };
      window.mount(config, states);
    }""", [stub, config or {}, states])
    pg.wait_for_timeout(300)

def stub(**kw): return {"devices": DEVICES, "screens": {}, "store": {"D1": {"pages": [{"id": "p1", "name": "Wohnen", "widgets": [dict(W1)]}], "pager": None}, "D2": {"pages": [{"id": "p1", "name": "Seite 1", "widgets": []}], "pager": None}}, **kw}

with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True, args=["--no-sandbox"])
    pg, errors = open_card(b, None)

    # ── no devices / no integration ────────────────────────────────────────────
    mount(pg, stub(devices={"devices": []}))
    check("no device yet: explains how to add one", "Display hinzufügen" in sh(pg, "r.textContent"), sh(pg, "r.textContent"))

    # ── loading ────────────────────────────────────────────────────────────────
    mount(pg, stub())
    check("first display selected, layout painted", sh(pg, "r.getElementById('dev').value") == "D1" and sh(pg, "r.querySelectorAll('.w').length") == 1)
    check("live value and unit in the preview", "21.5 °C" in sh(pg, "r.querySelector('.w .v').textContent"), sh(pg, "r.querySelector('.w .v').textContent"))
    check("the connected dot follows the sensor even with a German entity id (…_verbunden)", sh(pg, "r.getElementById('dot').classList.contains('on')"))
    check("save is disabled until something changes", sh(pg, "r.getElementById('save').disabled"))
    mount(pg, stub(), {"device": "bad"})
    check("config 'device' picks the display (case-insensitive) and fixes the select", sh(pg, "r.getElementById('dev').value") == "D2" and sh(pg, "r.getElementById('dev').disabled"))
    mount(pg, stub())

    # ── adding: ids must never collide with loaded widgets ─────────────────────
    pg.evaluate("() => card.shadowRoot.querySelector('.pi[data-type=gauge]').click()"); pg.wait_for_timeout(100)
    ids = sh(pg, "card._widgets.map(w => w.id)")
    check("a new widget gets a free id (w1 is taken by the loaded one)", ids == ["w1", "w2"], ids)
    check("new widget is selected, save enabled", sh(pg, "card._selected") == "w2" and not sh(pg, "r.getElementById('save').disabled"))

    # ── properties ─────────────────────────────────────────────────────────────
    pg.fill("display-card >> #p-entity", "sensor.humidity"); pg.wait_for_timeout(100)
    check("entity picker updates the preview with the live state", "40" in sh(pg, "r.querySelector('.w.gauge .v').textContent"), sh(pg, "r.querySelector('.w.gauge .v').textContent"))
    pg.fill("display-card >> #p-label", "<img src=x onerror=window.__xss=1>Feuchte"); pg.wait_for_timeout(100)
    check("labels are shown as text, never executed", pg.evaluate("() => window.__xss") is None and "<img" in sh(pg, "r.querySelector('.w.gauge .t').textContent"))
    pg.fill("display-card >> #p-label", "Feuchte")
    pg.fill("display-card >> #p-x", "9999"); pg.wait_for_timeout(50)
    check("typed position is clamped onto the screen", sh(pg, "card._find('w2').x + card._find('w2').w") <= 480, sh(pg, "card._find('w2').x"))

    # ── dragging and resizing in 5 px steps ────────────────────────────────────
    box = pg.evaluate("() => { const b = card.shadowRoot.querySelector('.w[data-id=w2]').getBoundingClientRect(); return [b.x, b.y, b.width, b.height] }")
    sc = sh(pg, "card._scale")
    pg.mouse.move(box[0] + 30, box[1] + 30); pg.mouse.down(); pg.mouse.move(box[0] + 30 - 9999, box[1] + 30 - 9999, steps=4); pg.mouse.up()
    check("dragging far off the top-left clamps at 0/0", sh(pg, "[card._find('w2').x, card._find('w2').y]") == [0, 0], sh(pg, "[card._find('w2').x, card._find('w2').y]"))
    box = pg.evaluate("() => { const b = card.shadowRoot.querySelector('.w[data-id=w2]').getBoundingClientRect(); return [b.x, b.y, b.width, b.height] }")
    pg.mouse.move(box[0] + 100 * sc, box[1] + 50 * sc); pg.mouse.down(); pg.mouse.move(box[0] + 100 * sc + 53 * sc, box[1] + 50 * sc + 27 * sc, steps=5); pg.mouse.up()
    pos = sh(pg, "[card._find('w2').x, card._find('w2').y]")
    check("moves snap to the 5 px grid", pos[0] % 5 == 0 and pos[1] % 5 == 0 and pos[0] > 0, pos)
    rz = pg.evaluate("() => { const b = card.shadowRoot.querySelector('.w[data-id=w2] .rz').getBoundingClientRect(); return [b.x + b.width / 2, b.y + b.height / 2] }")
    h0 = sh(pg, "card._find('w2').w")
    pg.mouse.move(rz[0], rz[1]); pg.mouse.down(); pg.mouse.move(rz[0] + 40 * sc, rz[1] + 20 * sc, steps=4); pg.mouse.up()
    check("the corner handle resizes (on grid, kept on screen)", sh(pg, "card._find('w2').w") > h0 and sh(pg, "card._find('w2').w") % 5 == 0 and sh(pg, "card._find('w2').x + card._find('w2').w") <= 480,
          sh(pg, "[card._find('w2').w, card._find('w2').x]"))

    # ── keyboard ───────────────────────────────────────────────────────────────
    x0 = sh(pg, "card._find('w2').x")
    pg.focus("display-card >> #screen"); pg.keyboard.press("ArrowRight"); pg.keyboard.press("Shift+ArrowRight")
    check("arrow keys nudge by 1 px, shift by 5 px", sh(pg, "card._find('w2').x") == x0 + 6, (x0, sh(pg, "card._find('w2').x")))

    # ── saving ─────────────────────────────────────────────────────────────────
    pg.click("display-card >> #save"); pg.wait_for_timeout(300)
    sent = calls(pg, "display/layout/set")[-1]
    check("save sends device id, the pages and no pager", sent["device_id"] == "D1" and [w["id"] for w in sent["pages"][0]["widgets"]] == ["w1", "w2"] and sent["pager"] is None and "service_domain" not in sent["pages"][0]["widgets"][1], sent)
    check("after saving the button is disabled again", sh(pg, "r.getElementById('save').disabled"))

    pg.evaluate("() => { card._find('w1').label = 'x'; card._setDirty(true) }")
    mount(pg, stub(saveError="widget 1: outside the 480x320 screen"))
    pg.evaluate("() => { card._find('w1').x = 5; card._setDirty(true) }")
    pg.click("display-card >> #save"); pg.wait_for_timeout(300)
    check("a server validation error is shown and the edit stays unsaved", "outside the 480x320" in sh(pg, "r.getElementById('msg').textContent") and not sh(pg, "r.getElementById('save').disabled"), sh(pg, "r.getElementById('msg').textContent"))

    # ── deleting ───────────────────────────────────────────────────────────────
    mount(pg, stub())
    pg.click("display-card >> .w[data-id=w1]"); pg.keyboard.press("Delete"); pg.wait_for_timeout(100)
    check("Delete removes the selected widget", sh(pg, "card._widgets.length") == 0 and sh(pg, "r.querySelectorAll('.w').length") == 0)

    # ── limit ──────────────────────────────────────────────────────────────────
    mount(pg, stub())
    for _ in range(40): pg.evaluate("() => card.shadowRoot.querySelector('.pi[data-type=label]').click()")
    check("at most 32 widgets", sh(pg, "card._widgets.length") == 32 and "32" in sh(pg, "r.getElementById('msg').textContent") and "Seite" in sh(pg, "r.getElementById('msg').textContent"), sh(pg, "card._widgets.length"))

    # ── unsaved changes guard when switching display ───────────────────────────
    mount(pg, stub())
    pg.evaluate("() => card._add('label', 100, 100)")
    pg.evaluate("() => { window.__confirm = false }"); pg.select_option("display-card >> #dev", "D2"); pg.wait_for_timeout(200)
    check("declining the prompt keeps the display and its edits", sh(pg, "card._deviceId") == "D1" and sh(pg, "r.getElementById('dev').value") == "D1" and sh(pg, "card._widgets.length") == 2)
    pg.evaluate("() => { window.__confirm = true }"); pg.select_option("display-card >> #dev", "D2"); pg.wait_for_timeout(300)
    check("accepting switches to the other display", sh(pg, "card._deviceId") == "D2" and sh(pg, "card._widgets.length") == 0)
    check("the offline panel shows an unlit dot", not sh(pg, "r.getElementById('dot').classList.contains('on')"))

    # ── live updates from Home Assistant ───────────────────────────────────────
    mount(pg, stub())
    pg.evaluate("() => { const h = { ...window.hass, states: { ...window.hass.states, 'sensor.temp': { entity_id: 'sensor.temp', state: '30.2', attributes: {} } } }; card.hass = h }"); pg.wait_for_timeout(100)
    check("a state change updates the preview without redrawing", "30.2" in sh(pg, "r.querySelector('.w .v').textContent"))

    # ── setup info (the one token) ─────────────────────────────────────────────
    mount(pg, stub())
    pg.click("display-card >> #setup"); pg.wait_for_timeout(200)
    check("setup box shows the URL, a masked device code and the prefilled link",
          sh(pg, "r.getElementById('si0').value") == "http://ha:8123" and sh(pg, "r.getElementById('si1').type") == "password"
          and sh(pg, "r.getElementById('si1').value") == "SECRET-KEY_1"
          and sh(pg, "r.getElementById('si2').value") == "http://192.168.4.1/?url=http%3A%2F%2Fha%3A8123&key=SECRET-KEY_1")
    check("no token is mentioned any more", "oken" not in sh(pg, "r.getElementById('setupbox').textContent"))
    pg.click("display-card >> [data-show=si1]")
    check("the device code can be revealed", sh(pg, "r.getElementById('si1').type") == "text")
    mount(pg, stub(notAdmin=True))
    pg.click("display-card >> #setup"); pg.wait_for_timeout(200)
    check("non-admins get a clear message instead of the token", "Administratoren" in sh(pg, "r.getElementById('msg').textContent") and "SECRET" not in sh(pg, "r.innerHTML"))

    # ── firmware update ────────────────────────────────────────────────────────
    def fw_state(state, installed, latest="0.3.0", **attrs):
        return {**STATES, "update.kuche_firmware": {"entity_id": "update.kuche_firmware", "state": state, "attributes": {"installed_version": installed, "latest_version": latest, **attrs}}}
    fw = lambda: sh(pg, "r.getElementById('fw').textContent")
    btn = lambda: sh(pg, "({hidden: r.getElementById('fwbtn').hidden, disabled: r.getElementById('fwbtn').disabled})")
    mount(pg, stub(), None, fw_state("on", "0.2.9"))
    check("an available update is announced with both versions and a button", fw() == "Firmware 0.2.9 → 0.3.0 verfügbar" and btn() == {"hidden": False, "disabled": False}, (fw(), btn()))
    pg.evaluate("() => { window.__confirm = false }"); pg.click("display-card >> #fwbtn"); pg.wait_for_timeout(100)
    check("declining the prompt installs nothing", not calls(pg, "service"))
    pg.evaluate("() => { window.__confirm = true }"); pg.click("display-card >> #fwbtn"); pg.wait_for_timeout(200)
    svc = calls(pg, "service")
    check("accepting calls update.install for this display's entity", svc and svc[-1]["domain"] == "update" and svc[-1]["service"] == "install" and svc[-1]["data"] == {"entity_id": "update.kuche_firmware"}, svc)
    check("success is reported", "aktualisiert" in sh(pg, "r.getElementById('msg').textContent"))
    pg.evaluate("() => { window.__serviceError = 'Display nicht verbunden' }"); pg.click("display-card >> #fwbtn"); pg.wait_for_timeout(200)
    check("a failed update shows Home Assistant's reason", "Display nicht verbunden" in sh(pg, "r.getElementById('msg').textContent"))
    pg.evaluate("() => { window.__serviceError = null }")
    mount(pg, stub(), None, fw_state("off", "0.3.0"))
    check("an up-to-date display only shows its version", fw() == "Firmware 0.3.0" and btn()["hidden"], (fw(), btn()))
    mount(pg, stub(), None, fw_state("on", "0.2.9", in_progress=True, update_percentage=42))
    check("while updating the percentage is shown and the button is locked", fw() == "Update läuft … 42 %" and btn() == {"hidden": False, "disabled": True}, (fw(), btn()))
    mount(pg, stub(), None, fw_state("unknown", None))
    check("an old firmware without version asks for one cable flash", "per Kabel" in fw() and btn()["hidden"], fw())
    mount(pg, stub(), None, {**STATES, "update.kuche_firmware": {"entity_id": "update.kuche_firmware", "state": "on", "attributes": {"installed_version": "0.2.9", "latest_version": "0.3.0"}}, "binary_sensor.kueche_verbunden": {"entity_id": "binary_sensor.kueche_verbunden", "state": "off", "attributes": {"device_class": "connectivity"}}})
    check("an offline display cannot be updated", btn() == {"hidden": False, "disabled": True}, btn())
    mount(pg, stub(), None, STATES)
    check("without an update entity nothing about firmware is shown", fw() == "" and btn()["hidden"])
    pg.select_option("display-card >> #dev", "D2"); pg.wait_for_timeout(200)
    check("a display without an update entity shows nothing either", fw() == "" and btn()["hidden"])

    # ── pages ──────────────────────────────────────────────────────────────────
    mount(pg, stub())
    chips = lambda: sh(pg, "[...r.querySelectorAll('#pages .chip[data-page]')].map(c => c.textContent)")
    check("the page bar lists the pages, the current one is highlighted", chips() == ["Wohnen"] and sh(pg, "r.querySelector('#pages .chip.on').textContent") == "Wohnen", chips())
    check("with nothing selected the side panel is about the page", "Seite 1 von 1" in sh(pg, "r.getElementById('props').textContent") and sh(pg, "r.getElementById('pg-name').value") == "Wohnen")
    check("a single page cannot be deleted or moved", sh(pg, "r.getElementById('pg-del').disabled && r.getElementById('pg-left').disabled && r.getElementById('pg-right').disabled"))
    pg.click("display-card >> #pg-add"); pg.wait_for_timeout(100)
    check("+ adds an empty page after the current one and opens it", chips() == ["Wohnen", "Seite 2"] and sh(pg, "card._page") == 1 and sh(pg, "card._widgets.length") == 0 and sh(pg, "r.querySelectorAll('.w').length") == 0)
    pg.fill("display-card >> #pg-name", "Bad"); pg.wait_for_timeout(50)
    check("renaming updates the chip", chips() == ["Wohnen", "Bad"], chips())
    pg.fill("display-card >> #pg-name", "ü" * 40); pg.wait_for_timeout(50)
    check("names are cut at 31 bytes (umlauts count double)", len(sh(pg, "card._pages[1].name").encode()) <= 31 and sh(pg, "card._pages[1].name").startswith("ü"), sh(pg, "card._pages[1].name"))
    pg.fill("display-card >> #pg-name", "ü" * 15); pg.wait_for_timeout(50)
    pg.evaluate("() => card._addPage(true)"); pg.wait_for_timeout(50)
    check("a copy of a long name is cut to fit instead of being rejected later", len(sh(pg, "card._pages[card._page].name").encode()) <= 31, sh(pg, "card._pages[card._page].name"))
    pg.evaluate("() => card._deletePage()"); pg.wait_for_timeout(50)
    pg.fill("display-card >> #pg-name", "Bad")
    pg.evaluate("() => card._add('label', 40, 40)"); pg.wait_for_timeout(50)
    check("widgets belong to the page they were added on, ids are per page", sh(pg, "card._pages[1].widgets.map(w => w.id)") == ["w1"] and sh(pg, "card._pages[0].widgets.length") == 1)
    pg.click("display-card >> .chip[data-page='0']"); pg.wait_for_timeout(100)
    check("switching pages shows that page's widgets", sh(pg, "card._page") == 0 and sh(pg, "r.querySelectorAll('.w').length") == 1 and "21.5" in sh(pg, "r.querySelector('.w .v').textContent"))
    pg.click("display-card >> #pg-right"); pg.wait_for_timeout(100)
    check("a page can be moved back", chips() == ["Bad", "Wohnen"] and sh(pg, "card._page") == 1, chips())
    pg.click("display-card >> #pg-dup"); pg.wait_for_timeout(100)
    check("duplicating copies the widgets onto a new page", len(chips()) == 3 and sh(pg, "card._widgets.length") == 1 and sh(pg, "card._pages[2].id") != sh(pg, "card._pages[1].id"), chips())
    pg.evaluate("() => { window.__confirm = false }"); pg.click("display-card >> #pg-del"); pg.wait_for_timeout(100)
    check("deleting a page with elements asks first", len(chips()) == 3)
    pg.evaluate("() => { window.__confirm = true }"); pg.click("display-card >> #pg-del"); pg.wait_for_timeout(100)
    check("after deleting, a neighbour page is shown", len(chips()) == 2 and 0 <= sh(pg, "card._page") < 2)
    for _ in range(10): pg.evaluate("() => card._addPage()")
    check("at most 8 pages", len(chips()) == 8 and sh(pg, "r.getElementById('pg-add').disabled"), len(chips()))

    # ── the page switcher ─────────────────────────────────────────────────────
    mount(pg, stub())
    pg.evaluate("() => card.shadowRoot.querySelector('.pi[data-type=pager]').click()"); pg.wait_for_timeout(100)
    pager = sh(pg, "({...card._pager})")
    check("the palette adds the switcher at the bottom centre and selects it", pager["y"] + pager["h"] <= 320 and pager["y"] > 200 and abs(pager["x"] + pager["w"] / 2 - 240) <= 5 and sh(pg, "card._selected") == "__pager", pager)
    check("it shows < name > and is dimmed while there is only one page", sh(pg, "r.querySelector('.w.pager .pl').textContent") == "Wohnen" and sh(pg, "r.querySelector('.w.pager').classList.contains('idle')") and sh(pg, "[...r.querySelectorAll('.w.pager .ar')].map(e => e.textContent)") == ["<", ">"])
    pg.evaluate("() => card.shadowRoot.querySelector('.pi[data-type=pager]').click()")
    check("a second click does not add a second one", sh(pg, "r.querySelectorAll('.w.pager').length") == 1)
    pg.select_option("display-card >> #p-mode", "count"); pg.wait_for_timeout(50)
    check("the mode switches the label to the page number", sh(pg, "r.querySelector('.w.pager .pl').textContent") == "1 / 1")
    pg.click("display-card >> #pg-add") if False else pg.evaluate("() => card._addPage()")
    pg.wait_for_timeout(100)
    check("with two pages it is no longer dimmed and counts", not sh(pg, "r.querySelector('.w.pager').classList.contains('idle')") and sh(pg, "r.querySelector('.w.pager .pl').textContent") == "2 / 2")
    check("it is on every page", sh(pg, "r.querySelectorAll('.w.pager').length") == 1)
    pg.evaluate("() => card._select('__pager')")
    pg.fill("display-card >> #p-w", "9999"); pg.wait_for_timeout(50); pg.fill("display-card >> #p-h", "5"); pg.wait_for_timeout(50)
    check("size and position are free but stay on screen and above a minimum", sh(pg, "card._pager.x + card._pager.w") <= 480 and sh(pg, "card._pager.h") >= 24, sh(pg, "[card._pager.x, card._pager.w, card._pager.h]"))
    box = pg.evaluate("() => { const b = card.shadowRoot.querySelector('.w.pager').getBoundingClientRect(); return [b.x, b.y, b.width, b.height] }"); sc = sh(pg, "card._scale")
    x0 = sh(pg, "card._pager.x"); y0 = sh(pg, "card._pager.y")
    pg.mouse.move(box[0] + box[2] / 2, box[1] + box[3] / 2); pg.mouse.down(); pg.mouse.move(box[0] + box[2] / 2 - 20 * sc, box[1] + box[3] / 2 - 30 * sc, steps=4); pg.mouse.up()
    check("the switcher can be dragged", sh(pg, "card._pager.y") < y0 and sh(pg, "card._pager.y") % 5 == 0, (x0, y0, sh(pg, "[card._pager.x, card._pager.y]")))
    pg.focus("display-card >> #screen"); pg.keyboard.press("Delete"); pg.wait_for_timeout(50)
    check("Delete removes the switcher again", sh(pg, "card._pager") is None and sh(pg, "r.querySelectorAll('.w.pager').length") == 0)
    pg.evaluate("() => { card._addPager(100, 250, false); card._pager.mode = 'count'; card._setDirty(true) }"); pg.click("display-card >> #save"); pg.wait_for_timeout(300)
    sent = calls(pg, "display/layout/set")[-1]
    check("saving sends the switcher without internal fields", sorted(sent["pager"]) == ["h", "mode", "w", "x", "y"] and sent["pager"]["mode"] == "count", sent["pager"])

    # ── portrait ──────────────────────────────────────────────────────────────
    mount(pg, stub(screens={"D1": {"w": 320, "h": 480}}))
    check("a portrait display gets a portrait canvas", sh(pg, "card._W") == 320 and sh(pg, "card._H") == 480 and "Hochformat" in sh(pg, "r.getElementById('res').textContent"), sh(pg, "r.getElementById('res').textContent"))
    dims = pg.evaluate("() => { const b = card.shadowRoot.getElementById('screen').getBoundingClientRect(); return [b.width, b.height] }")
    check("and fits a sensible height", dims[1] <= 641, dims)
    check("with the matching aspect ratio", abs(dims[0] / dims[1] - 320 / 480) < 0.01, dims)
    pg.evaluate("() => card._add('bar', 0, 0)"); pg.fill("display-card >> #p-w", "9999"); pg.wait_for_timeout(50)
    pg.evaluate("() => card._add('label', 0, 400)")
    check("widgets are clamped to the narrow screen", sh(pg, "card._widgets.every(w => w.x + w.w <= 320 && w.y + w.h <= 480)"))
    pg.evaluate("() => card._addPager(0, 0, true)")
    check("the switcher fits the portrait width", sh(pg, "card._pager.x + card._pager.w") <= 320 and sh(pg, "card._pager.y + card._pager.h") <= 480)

    # ── v0.4: colours, new elements ───────────────────────────────────────────
    S4 = {**STATES,
          "light.off": {"entity_id": "light.off", "state": "off", "attributes": {"brightness": None}},
          "sensor.bat": {"entity_id": "sensor.bat", "state": "85", "attributes": {"battery": 12}},
          "input_number.n": {"entity_id": "input_number.n", "state": "7", "attributes": {"min": 5, "max": 30}},
          "media_player.tv": {"entity_id": "media_player.tv", "state": "playing", "attributes": {"media_title": "Song <b>X</b>", "media_artist": "Band", "volume_level": 0.4, "source": "HDMI 1"}},
          "climate.c": {"entity_id": "climate.c", "state": "heat", "attributes": {"temperature": 21.5, "current_temperature": 20}}}
    mount(pg, stub(), None, S4)
    types = sh(pg, "[...r.querySelectorAll('.pi')].map(e => e.dataset.type)")
    check("the palette offers all element types", set(types) >= {"label", "button", "switch", "checkbox", "slider", "arc", "gauge", "bar", "led", "dropdown", "roller", "clock", "panel", "media", "thermostat", "pager"}, types)
    for t in ["label", "button", "switch", "checkbox", "slider", "arc", "gauge", "bar", "led", "dropdown", "roller", "clock", "panel", "media", "thermostat"]:
        pg.evaluate("(t) => { card._widgets.length = 0; card._add(t, 10, 10); }", t)
        check(f"{t}: added with minimum size and drawn", sh(pg, f"r.querySelectorAll('.w.{t}').length") == 1 and sh(pg, "card._widgets[0].w") >= (200 if t == "media" else 140 if t == "thermostat" else 20), t)
    pg.evaluate("() => { card._widgets.length = 0; card._add('button', 10, 10); }")
    pg.fill("display-card >> #p-entity", "light.off"); pg.wait_for_timeout(80)
    pg.fill("display-card >> #p-bg", "#112233"); pg.fill("display-card >> #p-on", "#00ff00"); pg.wait_for_timeout(80)
    bg = lambda: pg.evaluate("() => getComputedStyle(card.shadowRoot.querySelector('.w.button')).backgroundColor")
    check("a button off uses the off colour", bg() == "rgb(17, 34, 51)", bg())
    pg.fill("display-card >> #p-entity", "light.kitchen"); pg.wait_for_timeout(80)
    check("and the on colour while the entity is on", bg() == "rgb(0, 255, 0)", bg())
    pg.click("display-card >> #p-on-x"); pg.wait_for_timeout(80)
    check("Standard resets a colour to the theme default", sh(pg, "card._widgets[0].on") == "" and bg() == "rgb(79, 195, 247)", bg())
    pg.select_option("display-card >> #p-icon", "power"); pg.wait_for_timeout(50)
    check("a button can show a symbol", "⏻" in sh(pg, "r.querySelector('.w.button').textContent"))
    pg.evaluate("() => { card._widgets.length = 0; card._add('button', 10, 10); card._widgets[0].entity = 'media_player.tv'; card._select(null); card._select('w1'); }")
    opts = sh(pg, "[...r.querySelectorAll('#p-action option')].map(o => o.value)")
    check("a media button offers media actions", "next" in opts and "volume_up" in opts and "mute" in opts, opts)
    pg.select_option("display-card >> #p-action", "next"); pg.wait_for_timeout(80)
    check("choosing an action proposes a matching symbol", sh(pg, "card._widgets[0].action") == "next" and sh(pg, "card._widgets[0].icon") == "next")
    pg.evaluate("() => { card._widgets.length = 0; card._add('button', 10, 10); card._widgets[0].entity = 'climate.c'; card._select(null); card._select('w1'); }")
    pg.fill("display-card >> #p-entity", "climate.c"); pg.evaluate("() => card.shadowRoot.querySelector('#p-entity').dispatchEvent(new Event('change'))"); pg.wait_for_timeout(80)
    check("a climate button must have an action and gets one", sh(pg, "card._widgets[0].action") == "temp_up")
    pg.evaluate("() => { card._widgets.length = 0; card._add('slider', 10, 10); }")
    pg.fill("display-card >> #p-entity", "input_number.n"); pg.evaluate("() => card.shadowRoot.querySelector('#p-entity').dispatchEvent(new Event('change'))"); pg.wait_for_timeout(80)
    check("a slider takes its range from an input_number", sh(pg, "[card._widgets[0].min, card._widgets[0].max]") == [5, 30], sh(pg, "[card._widgets[0].min, card._widgets[0].max]"))
    pg.evaluate("() => { card._widgets.length = 0; card._add('bar', 10, 10); const w = card._widgets[0]; w.entity = 'sensor.bat'; w.warn = 60; w.crit = 80; card._select(null); card._select('w1'); }")
    fillc = lambda: pg.evaluate("() => getComputedStyle(card.shadowRoot.querySelector('.w.bar .track i')).backgroundColor")
    check("a bar turns red above its critical value", fillc() == "rgb(239, 83, 80)", fillc())
    pg.fill("display-card >> #p-crit", "90"); pg.wait_for_timeout(50)
    check("and amber above the warning value", fillc() == "rgb(255, 167, 38)", fillc())
    pg.fill("display-card >> #p-warn", ""); pg.fill("display-card >> #p-crit", ""); pg.wait_for_timeout(50)
    check("without thresholds it keeps the base colour", fillc() == "rgb(79, 195, 247)", fillc())
    pg.fill("display-card >> #p-warn", "90"); pg.check("display-card >> #p-invert"); pg.wait_for_timeout(50)
    check("inverted thresholds trigger below the value (85 <= 90)", fillc() == "rgb(255, 167, 38)", fillc())
    pg.fill("display-card >> #p-attr", "battery"); pg.wait_for_timeout(50)
    check("an attribute can replace the state", "12" in sh(pg, "r.querySelector('.w.bar .v').textContent"), sh(pg, "r.querySelector('.w.bar .v').textContent"))
    pg.evaluate("() => { card._widgets.length = 0; card._add('media', 10, 10); card._widgets[0].entity = 'media_player.tv'; card._select(null); card._select('w1'); const el = card.shadowRoot.querySelector('.w.media'); card._paint(el, card._widgets[0]); }")
    txt = sh(pg, "r.querySelector('.w.media').textContent")
    check("the media widget shows title (as text) and artist", "Song <b>X</b>" in txt and "Band" in txt and sh(pg, "r.querySelector('.w.media .t b')") is None, txt)
    pg.evaluate("() => { card._widgets.length = 0; card._add('clock', 10, 10); }")
    check("a clock shows the time", __import__("re").search(r"\d\d:\d\d", sh(pg, "r.querySelector('.w.clock').textContent")) is not None)
    pg.evaluate("() => { card._widgets.length = 0; card._add('clock', 10, 10); card._widgets[0].format = 'date'; card._select(null); card._select('w1'); card._paint(card.shadowRoot.querySelector('.w.clock'), card._widgets[0]); }")
    check("with the date format it shows a date", __import__("re").search(r"\d\d\.\d\d\.\d{4}", sh(pg, "r.querySelector('.w.clock').textContent")) is not None)
    check("a clock and a panel have no entity field", sh(pg, "r.getElementById('p-entity')") is None)
    pg.evaluate("() => { card._widgets.length = 0; card._add('media', 10, 10); }")
    pg.fill("display-card >> #p-w", "50"); pg.fill("display-card >> #p-h", "20"); pg.wait_for_timeout(50)
    check("the size cannot go below the element's minimum", sh(pg, "card._widgets[0].w") >= 200 and sh(pg, "card._widgets[0].h") >= 100, sh(pg, "[card._widgets[0].w, card._widgets[0].h]"))
    pg.evaluate("() => card._select(null)")
    pg.fill("display-card >> #p-pgbg", "#202040"); pg.wait_for_timeout(50)
    check("the page background colours the screen", pg.evaluate("() => getComputedStyle(card.shadowRoot.getElementById('screen')).backgroundColor") == "rgb(32, 32, 64)")
    pg.click("display-card >> #save"); pg.wait_for_timeout(300)
    sent = calls(pg, "display/layout/set")[-1]
    check("saving sends the page background and the new fields", sent["pages"][0]["bg"] == "#202040" and {"bg", "fg", "on", "attr", "icon", "action", "format", "warn", "crit", "invert"} <= set(sent["pages"][0]["widgets"][0]), sent["pages"][0])
    pg.evaluate("() => card._addPage(true)"); pg.wait_for_timeout(50)
    check("a duplicated page keeps its background", sh(pg, "card._pages[1].bg") == "#202040")
    pg.click("display-card >> #p-pgbg-x"); pg.wait_for_timeout(50)
    check("Standard clears the page background", sh(pg, "card._pages[1].bg") == "" and pg.evaluate("() => card.shadowRoot.getElementById('screen').style.background") == "")

    # ── font size ─────────────────────────────────────────────────────────────
    mount(pg, stub(), None, S4)
    pg.evaluate("() => { card._widgets.length = 0; card._add('label', 10, 10); card._widgets[0].entity = 'sensor.temp'; card._select(null); card._select('w1'); }")
    opts = sh(pg, "[...r.querySelectorAll('#p-font option')].map(o => o.value)")
    check("a label offers the font sizes (and automatic)", opts == ["0", "12", "14", "16", "20", "24"], opts)
    px = lambda sel: pg.evaluate("(q) => getComputedStyle(card.shadowRoot.querySelector(q)).fontSize", sel)
    auto = px(".w.label .v")
    pg.select_option("display-card >> #p-font", "24"); pg.wait_for_timeout(50)
    check("the size changes the preview text", px(".w.label .v") == "24px" and auto != "24px", (auto, px(".w.label .v")))
    pg.select_option("display-card >> #p-font", "0"); pg.wait_for_timeout(50)
    check("automatic restores the default", px(".w.label .v") == auto and sh(pg, "card._widgets[0].font") == 0)
    pg.evaluate("() => { card._widgets.length = 0; card._add('clock', 10, 10); }")
    check("a clock also offers the two big sizes", "48" in sh(pg, "[...r.querySelectorAll('#p-font option')].map(o => o.value)"))
    pg.select_option("display-card >> #p-font", "48"); pg.wait_for_timeout(50)
    check("and applies them", px(".w.clock") == "48px")
    pg.evaluate("() => { card._widgets.length = 0; card._add('bar', 10, 10); }")
    check("a bar has no font setting", sh(pg, "r.getElementById('p-font')") is None)
    pg.evaluate("() => { card._widgets.length = 0; card._add('button', 10, 10); card._widgets[0].label = 'Ok'; card._widgets[0].font = 20; card._setDirty(true); }"); pg.click("display-card >> #save"); pg.wait_for_timeout(300)
    check("saving sends the font size", calls(pg, "display/layout/set")[-1]["pages"][0]["widgets"][0]["font"] == 20)

    # ── click sound (Tastenton) ───────────────────────────────────────────────
    mount(pg, stub(click={"D1": False, "D2": True}), None, STATES)
    check("the Tastenton checkbox starts as the display has it (off)", sh(pg, "r.getElementById('click')") is not None and not sh(pg, "r.getElementById('click').checked"))
    pg.check("display-card >> #click"); pg.wait_for_timeout(200)
    sent = calls(pg, "display/settings/set")
    check("ticking it stores the setting at once, for this display", len(sent) == 1 and sent[0]["click"] is True and sent[0]["device_id"] == "D1", sent)
    check("it does not make the layout dirty", sh(pg, "r.getElementById('save').disabled"))
    pg.select_option("display-card >> #dev", "D2"); pg.wait_for_timeout(300)
    check("another display shows its own setting", sh(pg, "r.getElementById('click').checked"))
    pg.uncheck("display-card >> #click"); pg.wait_for_timeout(200)
    check("unticking sends false", calls(pg, "display/settings/set")[-1]["click"] is False)
    mount(pg, stub(click={"D1": True, "D2": False}), None, STATES)
    check("a display that has it on is shown ticked when loaded", sh(pg, "r.getElementById('click').checked"))
    pg.select_option("display-card >> #dev", "D2"); pg.wait_for_timeout(300)
    check("and the other one, which has it off, unticked", not sh(pg, "r.getElementById('click').checked"))
    mount(pg, stub(settingsError=True), None, STATES)
    pg.click("display-card >> #click"); pg.wait_for_timeout(200)
    check("a refused change is shown and the box goes back", not sh(pg, "r.getElementById('click').checked") and "nicht gespeichert" in sh(pg, "r.getElementById('msg').textContent"), sh(pg, "r.getElementById('msg').textContent"))

    # ── sidebar panel ─────────────────────────────────────────────────────────
    mount(pg, stub(), None, STATES)
    pg.add_script_tag(url="/display-panel.js"); pg.wait_for_timeout(200)
    pg.evaluate("""() => { const p = document.createElement('display-panel'); p.hass = window.hass; p.narrow = true; p.panel = {}; p.route = {}; document.body.innerHTML = ''; document.body.appendChild(p); window.panelEl = p; }""")
    pg.wait_for_timeout(500)
    check("the sidebar panel shows the editor with a menu button", pg.evaluate("() => !!panelEl.querySelector('display-card') && !!panelEl.querySelector('ha-menu-button')"))
    check("and the editor gets hass and works (device list loaded)", pg.evaluate("() => panelEl.querySelector('display-card').shadowRoot.querySelectorAll('#dev option').length") == 2)
    check("the menu button knows the narrow layout", pg.evaluate("() => panelEl.querySelector('ha-menu-button').narrow") is True)

    # ── inside Home Assistant the card must wait for the app's own element registry ──────────────────
    sim = b.new_page()
    sim.goto(f"http://127.0.0.1:{PORT}/ha-sim.html"); sim.wait_for_timeout(500)
    check("inside HA nothing is defined while the app has not started (HA swaps the registry later)",
          not sim.evaluate("() => !!customElements.get('display-card')") and not sim.evaluate("() => (window.customCards || []).length"))
    sim.evaluate("() => customElements.define('home-assistant', class extends HTMLElement {})"); sim.wait_for_timeout(300)
    check("once the app is up the card and its editor are defined and listed",
          sim.evaluate("() => !!customElements.get('display-card') && !!customElements.get('display-card-editor')")
          and sim.evaluate("() => window.customCards.map(c => c.type)") == ["display-card"])

    check("no JavaScript errors on the page", not errors, errors)
    b.close()

print("\n%d check(s) failed" % len(failed) if failed else "\nall checks passed")
sys.exit(1 if failed else 0)
