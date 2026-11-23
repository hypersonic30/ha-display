import pytest

from homeassistant.config_entries import ConfigEntryState
from homeassistant.helpers import entity_registry as er

from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, CONF_HA_URL, DOMAIN

from .conftest import device, make_token

W = {"id": "w1", "type": "label", "entity": "sensor.temp", "label": "Temp", "unit": "°C", "x": 10, "y": 10, "w": 150, "h": 70}


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


async def send(client, **msg) -> int:
    """Send a command and return the message id it was given."""
    await client.send_json_auto_id(msg)
    return msg["id"]


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
    assert reply["result"] == {"name": "Küche", "rev": 0, "widgets": [], "settings": {"brightness": 80}}
    assert (await call(admin, "display/layout/get", device_id="nope"))["error"]["code"] == "not_found"


async def test_save_validates_and_cleans(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"))
    dev = ids(entry)["Küche"]
    reply = await call(admin, "display/layout/set", device_id=dev, widgets=[{**W, "service_domain": "light", "evil": 1}])
    assert reply["success"] and reply["result"]["rev"] == 1
    saved = reply["result"]["widgets"][0]
    assert "service_domain" not in saved and "evil" not in saved and saved["min"] == 0 and saved["max"] == 100
    again = await call(admin, "display/layout/set", device_id=dev, widgets=[W])
    assert again["result"]["rev"] == 2
    assert (await call(admin, "display/layout/get", device_id=dev))["result"]["widgets"][0]["label"] == "Temp"

    bad = {
        "unknown type": [{**W, "type": "clock"}],
        "off screen": [{**W, "x": 400}],
        "bad entity": [{**W, "entity": "Not An Entity"}],
        "bad id": [{**W, "id": "has space"}],
        "duplicate id": [W, W],
        "label too long": [{**W, "label": "ü" * 20}],
        "gauge range": [{**W, "type": "gauge", "min": 5, "max": 5}],
        "missing x": [{k: v for k, v in W.items() if k != "x"}],
        "too many": [{**W, "id": f"w{i}", "x": 0, "y": 0, "w": 20, "h": 20} for i in range(33)],
    }
    for what, widgets in bad.items():
        reply = await call(admin, "display/layout/set", device_id=dev, widgets=widgets)
        assert not reply["success"] and reply["error"]["code"] == "invalid_format", what
    assert (await call(admin, "display/layout/get", device_id=dev))["result"]["rev"] == 2


async def test_only_admins_edit_and_see_the_device_key(hass, make_entry, user, admin):
    entry = await setup(hass, make_entry, device("Küche"))
    dev = ids(entry)["Küche"]
    assert (await call(user, "display/layout/set", device_id=dev, widgets=[W]))["error"]["code"] == "unauthorized"
    assert (await call(user, "display/setup_info", device_id=dev))["error"]["code"] == "unauthorized"
    assert (await call(user, "display/layout/get", device_id=dev))["success"]
    info = await call(admin, "display/setup_info", device_id=dev)
    assert info["result"] == {"name": "Küche", CONF_HA_URL: "http://ha.test:8123", CONF_DEVICE_KEY: "key-küche"}


async def test_layouts_of_removed_displays_are_dropped(hass, make_entry, admin):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    for name, dev in ids(entry).items():
        await call(admin, "display/layout/set", device_id=dev, widgets=[W])
    store = hass.data[DOMAIN]["store"]
    gone = ids(entry)["Bad"]
    hass.config_entries.async_remove_subentry(entry, gone)
    await hass.async_block_till_done()
    assert store.get(gone) == {"widgets": [], "rev": 0}
    assert store.get(ids(entry)["Küche"])["rev"] == 1


async def test_card_is_served_and_loaded_with_a_version(hass, make_entry, hass_client):
    await setup(hass, make_entry, device("Küche"))
    client = await hass_client()
    resp = await client.get("/display/static/display-card.js")
    assert resp.status == 200 and "display-card" in await resp.text()
    from homeassistant.components.frontend import DATA_EXTRA_MODULE_URL  # noqa: PLC0415
    assert "/display/static/display-card.js?v=0.2.0" in hass.data[DATA_EXTRA_MODULE_URL].urls
