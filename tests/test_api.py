import pytest
import voluptuous as vol

from homeassistant.config_entries import ConfigEntryState
from homeassistant.helpers import entity_registry as er

from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, CONF_HA_URL, CONF_ROTATION, DOMAIN
from custom_components.display.layouts import STORAGE_KEY, LayoutStore, validate_layout

from .conftest import device, make_token

W = {"id": "w1", "type": "label", "entity": "sensor.temp", "label": "Temp", "unit": "°C", "x": 10, "y": 10, "w": 150, "h": 70}
PAGE = {"id": "p1", "name": "Wohnen", "widgets": [W]}
PAGER = {"x": 100, "y": 270, "w": 200, "h": 40, "mode": "name"}


async def setup(hass, make_entry, *devices):
    entry = make_entry(*devices)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    return entry


def ids(entry):
    return {s.title: s.subentry_id for s in entry.subentries.values()}


@pytest.fixture
async def admin(hass_ws_client):
    return await hass_ws_client()


@pytest.fixture
async def user(hass, hass_ws_client):
    return await hass_ws_client(hass, await make_token(hass, name="reader"))


async def call(client, type_, **kw):
    await client.send_json_auto_id({"type": type_, **kw})
    return await client.receive_json()


async def test_devices_list_and_empty_layout(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    reply = await call(admin, "display/devices")
    assert reply["success"]
    assert [d["name"] for d in reply["result"]["devices"]] == ["Bad", "Küche"]
    assert all(d["online"] is False for d in reply["result"]["devices"])

    reply = await call(admin, "display/layout/get", device_id=ids(entry)["Küche"])
    assert reply["result"] == {
        "name": "Küche",
        "rev": 0,
        "pages": [{"id": "p1", "name": "Seite 1", "widgets": []}],
        "pager": None,
        "screen": {"w": 480, "h": 320},
        "settings": {"brightness": 80, "rotation": 0},
    }
    assert (await call(admin, "display/layout/get", device_id="nope"))["error"]["code"] == "not_found"


async def test_save_pages_validates_and_cleans(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"))
    dev = ids(entry)["Küche"]
    two = [{**PAGE, "name": " Wohnen "}, {"id": "p2", "name": "Bad", "widgets": [{**W, "service_domain": "light", "evil": 1}]}]
    reply = await call(admin, "display/layout/set", device_id=dev, pages=two, pager=PAGER)
    assert reply["success"] and reply["result"]["rev"] == 1
    saved = reply["result"]
    assert [p["name"] for p in saved["pages"]] == ["Wohnen", "Bad"] and saved["pager"] == PAGER
    widget = saved["pages"][1]["widgets"][0]
    assert "service_domain" not in widget and "evil" not in widget and widget["min"] == 0 and widget["max"] == 100
    assert (await call(admin, "display/layout/get", device_id=dev))["result"] == saved
    assert (await call(admin, "display/layout/set", device_id=dev, pages=[PAGE]))["result"]["pager"] is None

    def bad(**override):
        return {"pages": [PAGE], "pager": PAGER, **override}

    full = [{**W, "id": f"w{i}", "x": 0, "y": 0, "w": 20, "h": 20} for i in range(32)]
    cases = {
        "no pages": bad(pages=[]),
        "nine pages": bad(pages=[{"id": f"p{i}", "name": f"S{i}", "widgets": []} for i in range(9)]),
        "duplicate page id": bad(pages=[PAGE, PAGE]),
        "page without name": bad(pages=[{**PAGE, "name": "  "}]),
        "page name too long": bad(pages=[{**PAGE, "name": "ü" * 20}]),
        "page id with space": bad(pages=[{**PAGE, "id": "a b"}]),
        "page not an object": bad(pages=["x"]),
        "unknown widget type": bad(pages=[{**PAGE, "widgets": [{**W, "type": "clock"}]}]),
        "widget off screen": bad(pages=[{**PAGE, "widgets": [{**W, "x": 400}]}]),
        "bad entity": bad(pages=[{**PAGE, "widgets": [{**W, "entity": "Not An Entity"}]}]),
        "duplicate widget id": bad(pages=[{**PAGE, "widgets": [W, W]}]),
        "label too long": bad(pages=[{**PAGE, "widgets": [{**W, "label": "ü" * 20}]}]),
        "gauge range": bad(pages=[{**PAGE, "widgets": [{**W, "type": "gauge", "min": 5, "max": 5}]}]),
        "33 widgets on one page": bad(pages=[{**PAGE, "widgets": full + [{**W, "id": "extra", "x": 0, "y": 0, "w": 20, "h": 20}]}]),
        "pager off screen": bad(pager={**PAGER, "x": 400}),
        "pager too small": bad(pager={**PAGER, "w": 20}),
        "pager bad mode": bad(pager={**PAGER, "mode": "icons"}),
        "pager not an object": bad(pager="yes"),
    }
    for what, body in cases.items():
        reply = await call(admin, "display/layout/set", device_id=dev, **body)
        assert not reply["success"] and reply["error"]["code"] in ("invalid_format",), what
    # 32 widgets per PAGE are fine, on two pages
    ok = await call(admin, "display/layout/set", device_id=dev, pages=[{**PAGE, "widgets": full}, {"id": "p2", "name": "B", "widgets": full}])
    assert ok["success"]
    # errors name the page they are on
    reply = await call(admin, "display/layout/set", device_id=dev, pages=[PAGE, {"id": "p2", "name": "Bad", "widgets": [{**W, "x": 400}]}])
    assert "Bad" in reply["error"]["message"]


async def test_portrait_displays_have_their_own_bounds(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Flur", rotation=1))
    dev = ids(entry)["Flur"]
    assert (await call(admin, "display/layout/get", device_id=dev))["result"]["screen"] == {"w": 320, "h": 480}
    tall = {**W, "x": 0, "y": 400, "w": 320, "h": 70}                       # fits portrait, not landscape
    ok = await call(admin, "display/layout/set", device_id=dev, pages=[{**PAGE, "widgets": [tall]}], pager={**PAGER, "x": 60, "y": 430})
    assert ok["success"]
    wide = await call(admin, "display/layout/set", device_id=dev, pages=[{**PAGE, "widgets": [{**W, "x": 200, "w": 150}]}])
    assert not wide["success"]


async def test_changing_the_orientation_fits_the_layout_onto_the_new_screen(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"))
    dev = ids(entry)["Küche"]
    wide = {**W, "x": 300, "y": 250, "w": 170, "h": 60}
    await call(admin, "display/layout/set", device_id=dev, pages=[{**PAGE, "widgets": [wide]}, {"id": "p2", "name": "B", "widgets": [{**W, "w": 400}]}],
               pager={"x": 200, "y": 270, "w": 270, "h": 40, "mode": "name"})
    hass.config_entries.async_update_subentry(entry, entry.subentries[dev], data={**entry.subentries[dev].data, CONF_ROTATION: 1})
    await hass.async_block_till_done()

    got = (await call(admin, "display/layout/get", device_id=dev))["result"]
    assert got["screen"] == {"w": 320, "h": 480} and got["rev"] == 2
    for item in [w for p in got["pages"] for w in p["widgets"]] + [got["pager"]]:
        assert item["x"] >= 0 and item["y"] >= 0 and item["x"] + item["w"] <= 320 and item["y"] + item["h"] <= 480, item
    assert got["pages"][0]["widgets"][0]["x"] == 150 and got["pages"][1]["widgets"][0]["w"] == 320
    validate_layout(got["pages"], got["pager"], (320, 480))
    # unchanged orientation on a later reload does not touch it again
    hass.config_entries.async_update_subentry(entry, entry.subentries[dev], data={**entry.subentries[dev].data, CONF_BRIGHTNESS: 40})
    await hass.async_block_till_done()
    assert (await call(admin, "display/layout/get", device_id=dev))["result"]["rev"] == 2


async def test_the_flat_layout_of_v020_becomes_the_first_page(hass, hass_storage):
    hass_storage[STORAGE_KEY] = {"version": 1, "key": STORAGE_KEY, "data": {"D1": {"widgets": [W], "rev": 3}, "D2": {"widgets": [], "rev": 1}}}
    store = LayoutStore(hass)
    await store.async_load()
    old = store.get("D1")
    assert old["pages"] == [{"id": "p1", "name": "Seite 1", "widgets": [W]}] and old["pager"] is None and old["rev"] == 3


async def test_only_admins_edit_and_see_the_device_key(hass, make_entry, user, admin):
    entry = await setup(hass, make_entry, device("Küche"))
    dev = ids(entry)["Küche"]
    assert (await call(user, "display/layout/set", device_id=dev, pages=[PAGE]))["error"]["code"] == "unauthorized"
    assert (await call(user, "display/setup_info", device_id=dev))["error"]["code"] == "unauthorized"
    assert (await call(user, "display/layout/get", device_id=dev))["success"]
    info = await call(admin, "display/setup_info", device_id=dev)
    assert info["result"] == {"name": "Küche", CONF_HA_URL: "http://ha.test:8123", CONF_DEVICE_KEY: "key-küche"}


async def test_layouts_of_removed_displays_are_dropped(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    for name, dev in ids(entry).items():
        await call(admin, "display/layout/set", device_id=dev, pages=[PAGE])
    store = hass.data[DOMAIN]["store"]
    gone = ids(entry)["Bad"]
    hass.config_entries.async_remove_subentry(entry, gone)
    await hass.async_block_till_done()
    assert store.get(gone)["rev"] == 0 and store.get(gone)["pages"][0]["widgets"] == []
    assert store.get(ids(entry)["Küche"])["rev"] == 1


async def test_card_is_served_and_loaded_with_a_version(hass, make_entry, hass_client):
    await setup(hass, make_entry, device("Küche"))
    client = await hass_client()
    resp = await client.get("/display/static/display-card.js")
    assert resp.status == 200 and "display-card" in await resp.text()
    from homeassistant.components.frontend import DATA_EXTRA_MODULE_URL  # noqa: PLC0415
    import json  # noqa: PLC0415
    import pathlib  # noqa: PLC0415
    version = json.loads((pathlib.Path(__file__).parent.parent / "custom_components/display/manifest.json").read_text())["version"]
    assert f"/display/static/display-card.js?v={version}" in hass.data[DATA_EXTRA_MODULE_URL].urls
