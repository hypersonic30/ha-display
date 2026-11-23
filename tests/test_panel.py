"""The panels' endpoint: authentication by device key, layout + state push, taps mapped by the server."""
import asyncio

import pytest
from pytest_homeassistant_custom_component.common import async_mock_service

from homeassistant.config_entries import ConfigEntryState
from homeassistant.helpers import entity_registry as er

from custom_components.display import panel
from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, DOMAIN

from .conftest import device

BTN = {"id": "b1", "type": "button", "entity": "light.kitchen", "x": 0, "y": 0, "w": 100, "h": 60}
SW = {"id": "s1", "type": "switch", "entity": "switch.fan", "x": 0, "y": 70, "w": 100, "h": 60}
LBL = {"id": "l1", "type": "label", "entity": "sensor.temp", "unit": "°C", "x": 110, "y": 0, "w": 100, "h": 60}


@pytest.fixture
def expected_lingering_timers() -> bool:
    """aiohttp arms one last, harmless heartbeat timer when the server closes a WebSocket itself."""
    return True


@pytest.fixture(autouse=True)
def fast_actions(monkeypatch):
    monkeypatch.setattr(panel, "MIN_ACTION_GAP", 0)


async def setup(hass, make_entry, *devices):
    entry = make_entry(*devices)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    return entry


def dev_id(entry, name):
    return next(s.subentry_id for s in entry.subentries.values() if s.title == name)


async def save(hass, entry, name, widgets):
    from custom_components.display.layouts import validate_widgets  # noqa: PLC0415
    await hass.data[DOMAIN]["store"].async_set(dev_id(entry, name), validate_widgets(widgets))
    await hass.async_block_till_done()


async def connect(hass_client_no_auth, key="key-küche", hello=True):
    client = await hass_client_no_auth()
    ws = await client.ws_connect("/api/display/ws")
    if hello:
        await ws.send_json({"type": "hello", "key": key, "fw": "test"})
    return ws


async def recv(ws):
    return await asyncio.wait_for(ws.receive_json(), 3)


async def test_ping_needs_no_login(hass, make_entry, hass_client_no_auth):
    await setup(hass, make_entry, device("Küche"))
    resp = await (await hass_client_no_auth()).get("/api/display/ping")
    assert resp.status == 200 and await resp.json() == {"integration": "display"}


async def test_wrong_or_missing_key_is_refused(hass, make_entry, hass_client_no_auth):
    await setup(hass, make_entry, device("Küche"))
    for hello in ({"type": "hello", "key": "nope"}, {"type": "hello", "key": ""}, {"type": "hello"},
                  {"type": "hello", "key": 123}, {"type": "tap", "widget": "b1"}):
        client = await hass_client_no_auth()
        ws = await client.ws_connect("/api/display/ws")
        await ws.send_json(hello)
        assert await recv(ws) == {"type": "error", "code": "auth"}, hello
        assert (await ws.receive()).type.name in ("CLOSE", "CLOSING", "CLOSED")
    # garbage instead of JSON, and silence
    ws = await (await hass_client_no_auth()).ws_connect("/api/display/ws")
    await ws.send_str("not json")
    assert (await ws.receive()).type.name in ("CLOSE", "CLOSING", "CLOSED")


async def test_silent_connection_is_dropped(hass, make_entry, hass_client_no_auth, monkeypatch):
    monkeypatch.setattr(panel, "HELLO_TIMEOUT", 0.2)
    await setup(hass, make_entry, device("Küche"))
    ws = await connect(hass_client_no_auth, hello=False)
    assert (await asyncio.wait_for(ws.receive(), 3)).type.name in ("CLOSE", "CLOSING", "CLOSED")


async def test_each_panel_gets_its_own_layout_and_states(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche", brightness=60), device("Bad"))
    await save(hass, entry, "Küche", [BTN, LBL])
    await save(hass, entry, "Bad", [SW])
    hass.states.async_set("light.kitchen", "on")
    hass.states.async_set("sensor.temp", "21.5")

    ws = await connect(hass_client_no_auth)
    layout = await recv(ws)
    assert layout["type"] == "layout" and layout["name"] == "Küche" and layout["rev"] == 1
    assert layout["settings"] == {"brightness": 60} and [w["id"] for w in layout["widgets"]] == ["b1", "l1"]
    assert await recv(ws) == {"type": "states", "states": {"light.kitchen": "on", "sensor.temp": "21.5"}}

    other = await connect(hass_client_no_auth, key="key-bad")
    assert (await recv(other))["name"] == "Bad"
    assert await recv(other) == {"type": "states", "states": {"switch.fan": "unavailable"}}


async def test_state_changes_are_pushed_and_long_values_cut(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await save(hass, entry, "Küche", [LBL])
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)

    hass.states.async_set("sensor.temp", "22.0")
    assert await recv(ws) == {"type": "states", "states": {"sensor.temp": "22.0"}}
    hass.states.async_set("sensor.temp", "ä" * 40)
    pushed = (await recv(ws))["states"]["sensor.temp"]
    assert len(pushed.encode()) <= 47 and pushed.startswith("ä")
    hass.states.async_remove("sensor.temp")
    assert (await recv(ws))["states"] == {"sensor.temp": "unavailable"}
    hass.states.async_set("sensor.other", "1")          # not in the layout: nothing is sent
    hass.states.async_set("sensor.temp", "23")
    assert (await recv(ws))["states"] == {"sensor.temp": "23"}


async def test_layout_and_settings_changes_reach_a_connected_panel(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await save(hass, entry, "Küche", [LBL])
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)

    hass.states.async_set("light.kitchen", "off")
    await save(hass, entry, "Küche", [LBL, BTN])             # now also watches light.kitchen
    layout = await recv(ws)
    assert layout["rev"] == 2 and len(layout["widgets"]) == 2
    assert (await recv(ws))["states"] == {"light.kitchen": "off", "sensor.temp": "unavailable"}
    hass.states.async_set("light.kitchen", "on")
    assert await recv(ws) == {"type": "states", "states": {"light.kitchen": "on"}}

    hass.config_entries.async_update_subentry(entry, entry.subentries[dev_id(entry, "Küche")], data={CONF_BRIGHTNESS: 30, CONF_DEVICE_KEY: "key-küche"})
    await hass.async_block_till_done()
    assert (await recv(ws))["settings"] == {"brightness": 30}


async def test_taps_are_mapped_by_the_server_from_the_layout(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    widgets = [BTN, SW, LBL,
               {"id": "b2", "type": "button", "entity": "scene.movie", "x": 0, "y": 140, "w": 50, "h": 50},
               {"id": "b3", "type": "button", "entity": "button.bell", "x": 60, "y": 140, "w": 50, "h": 50},
               {"id": "b4", "type": "button", "entity": "script.go", "x": 120, "y": 140, "w": 50, "h": 50}]
    await save(hass, entry, "Küche", widgets)
    toggles = async_mock_service(hass, "homeassistant", "toggle")
    on = async_mock_service(hass, "homeassistant", "turn_on")
    off = async_mock_service(hass, "homeassistant", "turn_off")
    scene_on = async_mock_service(hass, "scene", "turn_on")
    script_on = async_mock_service(hass, "script", "turn_on")
    press = async_mock_service(hass, "button", "press")
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)

    async def send(**msg):
        await ws.send_json(msg)
        await hass.async_block_till_done(); await asyncio.sleep(0.05); await hass.async_block_till_done()

    await send(type="tap", widget="b1")
    await send(type="tap", widget="b2")
    await send(type="tap", widget="b3")
    await send(type="tap", widget="b4")
    await send(type="set", widget="s1", on=True)
    await send(type="set", widget="s1", on=False)
    assert [c.data["entity_id"] for c in toggles] == ["light.kitchen"]
    assert [c.data["entity_id"] for c in scene_on] == ["scene.movie"]
    assert [c.data["entity_id"] for c in press] == ["button.bell"]
    assert [c.data["entity_id"] for c in script_on] == ["script.go"]
    assert [c.data["entity_id"] for c in on] == ["switch.fan"] and [c.data["entity_id"] for c in off] == ["switch.fan"]

    # everything else is ignored: unknown widget, wrong kind of message for the widget, non-bool, extra fields
    before = len(toggles) + len(on) + len(off)
    for msg in ({"type": "tap", "widget": "nope"}, {"type": "tap", "widget": "s1"}, {"type": "set", "widget": "b1", "on": True},
                {"type": "tap", "widget": "l1"}, {"type": "set", "widget": "s1", "on": "yes"}, {"type": "set", "widget": "s1"},
                {"type": "tap", "entity": "lock.front_door", "domain": "lock", "service": "unlock"}, {"type": "call_service"}, [1, 2]):
        await send(**msg) if isinstance(msg, dict) else (await ws.send_json(msg))
    await hass.async_block_till_done()
    assert len(toggles) + len(on) + len(off) == before


async def test_taps_are_rate_limited(hass, make_entry, hass_client_no_auth, monkeypatch):
    monkeypatch.setattr(panel, "MIN_ACTION_GAP", 5)
    entry = await setup(hass, make_entry, device("Küche"))
    await save(hass, entry, "Küche", [BTN])
    toggles = async_mock_service(hass, "homeassistant", "toggle")
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)
    for _ in range(5):
        await ws.send_json({"type": "tap", "widget": "b1"})
    await asyncio.sleep(0.1); await hass.async_block_till_done()
    assert len(toggles) == 1


async def test_a_layout_cannot_give_a_panel_dangerous_entities(hass, make_entry):
    from custom_components.display.layouts import validate_widgets  # noqa: PLC0415
    import voluptuous as vol  # noqa: PLC0415
    for entity in ("lock.front_door", "alarm_control_panel.home", "climate.living"):
        with pytest.raises(vol.Invalid):
            validate_widgets([{**BTN, "entity": entity}])
        with pytest.raises(vol.Invalid):
            validate_widgets([{**SW, "entity": entity}])
    validate_widgets([{**LBL, "entity": "lock.front_door"}])          # showing a state is fine


async def test_connected_sensor_follows_the_session(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    entity_id = er.async_get(hass).async_get_entity_id("binary_sensor", DOMAIN, f"{dev_id(entry, 'Küche')}_connected")
    assert hass.states.get(entity_id).state == "off"
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)
    await hass.async_block_till_done()
    assert hass.states.get(entity_id).state == "on"
    await ws.close()
    await asyncio.sleep(0.1); await hass.async_block_till_done()
    assert hass.states.get(entity_id).state == "off"


async def test_renewing_the_key_or_removing_the_display_disconnects_the_panel(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)
    hass.config_entries.async_update_subentry(entry, entry.subentries[dev_id(entry, "Küche")], data={CONF_BRIGHTNESS: 80, CONF_DEVICE_KEY: "renewed"})
    await hass.async_block_till_done()
    assert (await asyncio.wait_for(ws.receive(), 3)).type.name in ("CLOSE", "CLOSING", "CLOSED")
    again = await connect(hass_client_no_auth)                       # the old key no longer works
    assert (await recv(again))["code"] == "auth"
    renewed = await connect(hass_client_no_auth, key="renewed")
    assert (await recv(renewed))["type"] == "layout"
    await recv(renewed)

    hass.config_entries.async_remove_subentry(entry, dev_id(entry, "Küche"))
    await hass.async_block_till_done()
    assert (await asyncio.wait_for(renewed.receive(), 3)).type.name in ("CLOSE", "CLOSING", "CLOSED")


async def test_removing_the_integration_disconnects_panels_and_refuses_new_ones(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)
    await hass.config_entries.async_remove(entry.entry_id)
    await hass.async_block_till_done()
    assert (await asyncio.wait_for(ws.receive(), 3)).type.name in ("CLOSE", "CLOSING", "CLOSED")
    assert (await recv(await connect(hass_client_no_auth)))["code"] == "auth"


async def test_the_server_refuses_dangerous_domains_even_if_a_layout_got_past_validation(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    sneaky = [{**BTN, "id": "b9", "entity": "lock.front_door", "label": "", "unit": "", "min": 0, "max": 100}]
    await hass.data[DOMAIN]["store"].async_set(dev_id(entry, "Küche"), sneaky)      # bypasses validate_widgets
    toggles = async_mock_service(hass, "homeassistant", "toggle")
    unlocks = async_mock_service(hass, "lock", "unlock")
    ws = await connect(hass_client_no_auth)
    await recv(ws); await recv(ws)
    await ws.send_json({"type": "tap", "widget": "b9"})
    await asyncio.sleep(0.1); await hass.async_block_till_done()
    assert toggles == [] and unlocks == []


async def test_the_first_snapshot_is_cut_like_later_updates(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await save(hass, entry, "Küche", [LBL])
    hass.states.async_set("sensor.temp", "x" * 200)
    ws = await connect(hass_client_no_auth)
    await recv(ws)
    assert len((await recv(ws))["states"]["sensor.temp"].encode()) <= 47
