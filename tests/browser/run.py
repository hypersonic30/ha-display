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
window.__stub = {};
window.mount = (config, states) => {
  const hass = { states, callWS: async (msg) => {
      window.__calls.push(JSON.parse(JSON.stringify(msg)));
      const h = window.__stub[msg.type];
      if (!h) throw { code: "not_found", message: "no stub for " + msg.type };
      return typeof h === "function" ? h(msg) : h;
  } };
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
        if self.path == "/test.html":
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
}
W1 = {"id": "w1", "type": "label", "entity": "sensor.temp", "label": "Temp", "unit": "°C", "min": 0, "max": 100, "x": 10, "y": 10, "w": 150, "h": 70}
DEVICES = {"devices": [{"id": "D1", "name": "Küche", "online": True}, {"id": "D2", "name": "Bad", "online": False}]}

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
        "display/layout/get": (m) => ({ name: "x", rev: 1, widgets: store[m.device_id] || [], settings: { brightness: 80 } }),
        "display/layout/set": (m) => { if (stub.saveError) throw { code: "invalid_format", message: stub.saveError }; store[m.device_id] = m.widgets; return { rev: 2, widgets: m.widgets }; },
        "display/setup_info": (m) => { if (stub.notAdmin) throw { code: "unauthorized", message: "Unauthorized" }; return { name: "Küche", ha_url: "http://ha:8123", device_key: "SECRET-KEY_1" }; },
      };
      window.mount(config, states);
    }""", [stub, config or {}, states])
    pg.wait_for_timeout(300)

def stub(**kw): return {"devices": DEVICES, "store": {"D1": [dict(W1)], "D2": []}, **kw}

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
    check("connected dot is on for an online panel", sh(pg, "r.getElementById('dot').classList.contains('on')"))
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
    check("save sends device id and the plain widget list", sent["device_id"] == "D1" and [w["id"] for w in sent["widgets"]] == ["w1", "w2"] and "service_domain" not in sent["widgets"][1], sent)
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
    check("at most 32 widgets", sh(pg, "card._widgets.length") == 32 and "32" in sh(pg, "r.getElementById('msg').textContent"), sh(pg, "card._widgets.length"))

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

    check("no JavaScript errors on the page", not errors, errors)
    b.close()

print("\n%d check(s) failed" % len(failed) if failed else "\nall checks passed")
sys.exit(1 if failed else 0)
