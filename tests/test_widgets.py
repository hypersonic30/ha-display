"""v0.4 widgets: colours, wire format, derived state keys, options/time frames and what each control may do."""
import asyncio

import pytest
import voluptuous as vol
from pytest_homeassistant_custom_component.common import async_mock_service

from custom_components.display import panel
from custom_components.display.layouts import validate_widgets

from .conftest import device
from .test_panel import connect, dev_id, recv, save, setup  # noqa: F401  (setup/save used below)


@pytest.fixture
def expected_lingering_timers() -> bool:
    return True


@pytest.fixture(autouse=True)
def fast_actions(monkeypatch):
    monkeypatch.setattr(panel, "MIN_ACTION_GAP", 0)
    monkeypatch.setattr(panel, "VALUE_GAP", 0)


def w(kind, entity="", **extra):
    return {"id": extra.pop("id", "w1"), "type": kind, "entity": entity, "x": 0, "y": 0, "w": 220, "h": 120, **extra}


async def session(hass, make_entry, hass_client_no_auth, widgets, states=None, page_bg=""):
    entry = await setup(hass, make_entry, device("Küche"))
    for entity, (state, attrs) in (states or {}).items():
        hass.states.async_set(entity, state, attrs)
    from custom_components.display.devices import screen_size  # noqa: PLC0415
    from custom_components.display.layouts import validate_layout  # noqa: PLC0415
    pages, pager = validate_layout([{"id": "p1", "name": "S", "bg": page_bg, "widgets": widgets}], None, screen_size(0))
    await hass.data["display"]["store"].async_set(dev_id(entry, "Küche"), pages, pager, screen_size(0))
    await hass.async_block_till_done()
    ws = await connect(hass_client_no_auth)
    frames = [await recv(ws)]
    frames.append(await recv(ws))
    return ws, frames


async def drain(ws, n=2):
    return [await recv(ws) for _ in range(n)]


# ── validation ───────────────────────────────────────────────────────────────

def test_colours_are_hex_only_and_lower_cased():
    ok = validate_widgets([w("button", "light.a", bg="#AABBCC", on="#00ff00", fg="")])
    assert ok[0]["bg"] == "#aabbcc" and ok[0]["on"] == "#00ff00" and ok[0]["fg"] == ""
    for bad in ("red", "#abc", "#gggggg", "aabbcc", "#aabbccdd", 5):
        with pytest.raises(vol.Invalid):
            validate_widgets([w("button", "light.a", bg=bad)])


def test_each_type_only_accepts_the_entities_it_can_work_with():
    validate_widgets([
        w("slider", "light.a", id="a"), w("arc", "cover.b", id="b", y=0), w("slider", "input_number.n", id="c", min=5, max=30),
        w("dropdown", "select.s", id="d"), w("roller", "input_select.s", id="e"), w("dropdown", "media_player.m", id="f"),
        w("media", "media_player.m", id="g"), w("thermostat", "climate.c", id="h"), w("clock", id="i"), w("panel", id="j"),
        w("led", "binary_sensor.door", id="k"), w("checkbox", "switch.s", id="l"), w("button", "media_player.m", id="m", action="next"),
        w("button", "climate.c", id="n", action="temp_up"), w("button", "vacuum.v", id="o"), w("button", "cover.c", id="p", action="open"),
    ])
    bad = [
        w("slider", "switch.a"), w("arc", "sensor.x"), w("dropdown", "light.a"), w("media", "light.a"), w("thermostat", "media_player.m"),
        w("media", ""), w("slider", ""), w("clock", "sensor.x"), w("panel", "light.a"), w("checkbox", "lock.door"),
        w("button", "light.a", action="next"), w("button", "climate.c"), w("label", "sensor.x", action="next"),
        w("media", "media_player.m", w=150), w("thermostat", "climate.c", h=60), w("arc", "light.a", w=40, h=40),
        w("slider", "light.a", min=10, max=10),
    ]
    for widget in bad:
        with pytest.raises(vol.Invalid):
            validate_widgets([widget])


def test_unknown_icons_actions_formats_and_attrs_are_refused():
    for extra in ({"icon": "rocket"}, {"format": "iso"}, {"attr": "Bad Attr"}, {"attr": "x" * 24}, {"warn": "high"}, {"invert": 1}):
        with pytest.raises(vol.Invalid):
            validate_widgets([w("label", "sensor.x", **extra)])
    ok = validate_widgets([w("bar", "sensor.x", warn=60, crit=80, invert=True, attr="battery", icon="battery")])[0]
    assert ok["warn"] == 60.0 and ok["crit"] == 80.0 and ok["invert"] is True


def test_old_layouts_without_the_new_fields_still_validate_with_defaults():
    old = {"id": "a", "type": "bar", "entity": "sensor.x", "label": "", "unit": "", "min": 0, "max": 100, "x": 0, "y": 0, "w": 100, "h": 30}
    clean = validate_widgets([old])[0]
    assert clean["bg"] == "" and clean["warn"] is None and clean["invert"] is False and clean["format"] == "time"


# ── what the panel is sent ───────────────────────────────────────────────────

async def test_wire_format_only_carries_what_differs_from_the_defaults(hass, make_entry, hass_client_no_auth):
    ws, (layout, _) = await session(hass, make_entry, hass_client_no_auth,
                                    [w("button", "light.a", label="Licht", bg="#112233", icon="power"), w("label", "sensor.x", id="w2")],
                                    page_bg="#000011")
    assert layout["page"]["bg"] == "#000011"
    first, second = layout["widgets"]
    assert first["bg"] == "#112233" and first["icon"] == "power" and first["label"] == "Licht"
    assert set(second) == {"id", "type", "entity", "x", "y", "w", "h", "min", "max"}
    await ws.close()


async def test_a_page_without_colour_has_no_bg_key(hass, make_entry, hass_client_no_auth):
    ws, (layout, _) = await session(hass, make_entry, hass_client_no_auth, [w("label", "sensor.x")])
    assert "bg" not in layout["page"]
    await ws.close()


async def test_state_keys_per_widget_type(hass, make_entry, hass_client_no_auth):
    states = {
        "light.a": ("on", {"brightness": 128}),
        "cover.c": ("open", {"current_position": 40}),
        "fan.f": ("on", {"percentage": 66}),
        "input_number.n": ("12.5", {}),
        "media_player.m": ("playing", {"media_title": "Song", "media_artist": "Band", "volume_level": 0.37, "source": "TV", "source_list": ["TV", "Radio"]}),
        "climate.c": ("heat", {"current_temperature": 20.5, "temperature": 22.0}),
        "sensor.x": ("77", {"battery": 31.0}),
        "select.s": ("b", {"options": ["a", "b", "c"]}),
    }
    widgets = [
        w("slider", "light.a", id="s1"), w("arc", "cover.c", id="s2"), w("slider", "fan.f", id="s3"),
        w("slider", "input_number.n", id="s4", min=0, max=30), w("media", "media_player.m", id="m"),
        w("thermostat", "climate.c", id="t"), w("label", "sensor.x", id="l", attr="battery"), w("dropdown", "select.s", id="d"),
        w("roller", "media_player.m", id="r"),
    ]
    ws, (_, st) = await session(hass, make_entry, hass_client_no_auth, widgets, states)
    got = st["states"]
    assert got["light.a|value"] == "50" and got["cover.c|value"] == "40" and got["fan.f|value"] == "66" and got["input_number.n|value"] == "12.5"
    assert got["media_player.m"] == "playing" and got["media_player.m|media_title"] == "Song" and got["media_player.m|media_artist"] == "Band"
    assert got["media_player.m|value"] == "37"
    assert got["climate.c"] == "heat" and got["climate.c|current_temperature"] == "20.5" and got["climate.c|temperature"] == "22"
    assert got["sensor.x"] == "77" and got["sensor.x|battery"] == "31"
    assert got["select.s|option"] == "b" and got["media_player.m|option"] == "TV"
    options = {m["widget"]: m["items"] for m in await drain(ws, 2)}
    assert options == {"d": ["a", "b", "c"], "r": ["TV", "Radio"]}
    await ws.close()


async def test_unknown_entity_and_missing_attribute(hass, make_entry, hass_client_no_auth):
    ws, (_, st) = await session(hass, make_entry, hass_client_no_auth, [w("label", "sensor.none", attr="foo"), w("slider", "light.gone", id="s")])
    assert st["states"] == {"sensor.none": "unavailable", "sensor.none|foo": "", "light.gone|value": ""}
    await ws.close()


async def test_derived_values_follow_state_changes_and_options_are_resent_only_when_changed(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("dropdown", "select.s"), w("slider", "light.a", id="s")],
                          {"select.s": ("a", {"options": ["a", "b"]}), "light.a": ("off", {})})
    await recv(ws)                                                       # options
    hass.states.async_set("light.a", "on", {"brightness": 255})
    assert await recv(ws) == {"type": "states", "states": {"light.a|value": "100"}}
    hass.states.async_set("select.s", "b", {"options": ["a", "b"]})
    assert await recv(ws) == {"type": "states", "states": {"select.s|option": "b"}}
    with pytest.raises(asyncio.TimeoutError):
        await asyncio.wait_for(ws.receive_json(), 0.3)                  # same options: no new frame
    hass.states.async_set("select.s", "b", {"options": ["a", "b", "c"]})
    await recv(ws)
    assert await recv(ws) == {"type": "options", "widget": "w1", "items": ["a", "b", "c"]}
    await ws.close()


async def test_options_are_limited_and_shortened(hass, make_entry, hass_client_no_auth):
    many = [f"opt{i}" for i in range(30)] + []
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("dropdown", "select.s")], {"select.s": ("opt1", {"options": many})})
    items = (await recv(ws))["items"]
    assert len(items) == 16
    hass.states.async_set("select.s", "opt1", {"options": ["ä" * 40]})
    await recv(ws)
    items = (await recv(ws))["items"]
    assert len(items[0].encode()) <= 31
    await ws.close()


async def test_time_frame_only_with_a_clock(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("clock", id="c1", format="date")])
    frame = await recv(ws)
    assert frame["type"] == "time" and isinstance(frame["epoch"], int) and isinstance(frame["offset"], int)
    await ws.close()
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("label", "sensor.x")])
    with pytest.raises(asyncio.TimeoutError):
        await asyncio.wait_for(ws.receive_json(), 0.3)
    await ws.close()


# ── what a panel may do ──────────────────────────────────────────────────────

async def test_sliders_map_to_the_right_service_per_domain(hass, make_entry, hass_client_no_auth):
    widgets = [
        w("slider", "light.a", id="li"), w("slider", "cover.c", id="co"), w("slider", "fan.f", id="fa"),
        w("slider", "media_player.m", id="me"), w("slider", "input_number.n", id="in", min=5, max=30), w("arc", "number.z", id="nu", min=0, max=10),
    ]
    ws, _ = await session(hass, make_entry, hass_client_no_auth, widgets)
    on = async_mock_service(hass, "light", "turn_on")
    off = async_mock_service(hass, "light", "turn_off")
    pos = async_mock_service(hass, "cover", "set_cover_position")
    pct = async_mock_service(hass, "fan", "set_percentage")
    vol_ = async_mock_service(hass, "media_player", "volume_set")
    inp = async_mock_service(hass, "input_number", "set_value")
    num = async_mock_service(hass, "number", "set_value")
    for wid, value in (("li", 40), ("li", 0), ("co", 150), ("fa", 12.4), ("me", 25), ("in", 99), ("nu", -3)):
        await ws.send_json({"type": "set", "widget": wid, "value": value})
        await asyncio.sleep(0.05)
    await hass.async_block_till_done()
    assert [c.data for c in on] == [{"entity_id": "light.a", "brightness_pct": 40}]
    assert [c.data for c in off] == [{"entity_id": "light.a"}]
    assert pos[0].data["position"] == 100                                # clamped
    assert pct[0].data["percentage"] == 12
    assert vol_[0].data == {"entity_id": "media_player.m", "volume_level": 0.25}
    assert inp[0].data["value"] == 30 and num[0].data["value"] == 0      # clamped to the widget range
    await ws.close()


async def test_bad_values_are_ignored(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("slider", "light.a")])
    on = async_mock_service(hass, "light", "turn_on")
    for bad in (True, "50", None, float("nan"), [1], {"v": 1}):
        await ws.send_json({"type": "set", "widget": "w1", "value": bad})
    await ws.send_json({"type": "set", "widget": "w1", "on": True})
    await ws.send_json({"type": "tap", "widget": "w1"})
    await asyncio.sleep(0.1)
    assert not on
    await ws.close()


async def test_dropdown_only_accepts_listed_options(hass, make_entry, hass_client_no_auth):
    widgets = [w("dropdown", "select.s", id="d1"), w("roller", "input_select.i", id="d2"), w("dropdown", "media_player.m", id="d3")]
    ws, _ = await session(hass, make_entry, hass_client_no_auth, widgets, {
        "select.s": ("a", {"options": ["a", "b"]}), "input_select.i": ("x", {"options": ["x", "y"]}),
        "media_player.m": ("on", {"source_list": ["TV", "Radio"]})})
    await drain(ws, 3)
    sel = async_mock_service(hass, "select", "select_option")
    isel = async_mock_service(hass, "input_select", "select_option")
    src = async_mock_service(hass, "media_player", "select_source")
    for wid, option in (("d1", "b"), ("d1", "evil"), ("d2", "y"), ("d3", "Radio"), ("d3", "x"), ("d1", 5)):
        await ws.send_json({"type": "set", "widget": wid, "option": option})
        await asyncio.sleep(0.05)
    await hass.async_block_till_done()
    assert [c.data["option"] for c in sel] == ["b"] and [c.data["option"] for c in isel] == ["y"] and [c.data["source"] for c in src] == ["Radio"]
    await ws.close()


async def test_media_widget_acts_and_sets_volume(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("media", "media_player.m", id="m")])
    calls = {name: async_mock_service(hass, "media_player", name) for name in ("media_previous_track", "media_play_pause", "media_next_track", "volume_set", "media_stop")}
    for action in ("prev", "play_pause", "next", "stop", "volume_up", "x", 3):
        await ws.send_json({"type": "act", "widget": "m", "action": action})
        await asyncio.sleep(0.03)
    await ws.send_json({"type": "set", "widget": "m", "value": 60})
    await asyncio.sleep(0.05)
    await hass.async_block_till_done()
    assert [len(calls[n]) for n in ("media_previous_track", "media_play_pause", "media_next_track", "media_stop")] == [1, 1, 1, 0]
    assert calls["volume_set"][0].data["volume_level"] == 0.6
    await ws.close()


async def test_thermostat_steps_within_limits(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("thermostat", "climate.c", id="t"), w("button", "climate.c", id="up", action="temp_up", y=130, h=40)],
                          {"climate.c": ("heat", {"temperature": 21.5, "max_temp": 22.0, "min_temp": 7, "target_temp_step": 0.5})})
    calls = async_mock_service(hass, "climate", "set_temperature")
    await ws.send_json({"type": "act", "widget": "t", "action": "temp_up"})
    await asyncio.sleep(0.03)
    await ws.send_json({"type": "act", "widget": "t", "action": "temp_down"})
    await asyncio.sleep(0.03)
    hass.states.async_set("climate.c", "heat", {"temperature": 22.0, "max_temp": 22.0, "min_temp": 7})
    await recv(ws)
    await ws.send_json({"type": "tap", "widget": "up"})
    await asyncio.sleep(0.05)
    await hass.async_block_till_done()
    assert [c.data["temperature"] for c in calls] == [22.0, 21.0, 22.0]    # never above max_temp
    await ws.close()


async def test_button_actions(hass, make_entry, hass_client_no_auth):
    widgets = [
        w("button", "media_player.m", id="pp"), w("button", "media_player.m", id="mu", action="mute"), w("button", "media_player.m", id="vu", action="volume_up"),
        w("button", "cover.c", id="co"), w("button", "cover.c", id="op", action="open"), w("button", "vacuum.v", id="va"),
        w("button", "vacuum.v", id="vh", action="return_home"),
    ]
    ws, _ = await session(hass, make_entry, hass_client_no_auth, widgets, {"media_player.m": ("playing", {"is_volume_muted": True})})
    names = {("media_player", "media_play_pause"), ("media_player", "volume_mute"), ("media_player", "volume_up"), ("cover", "toggle"),
             ("cover", "open_cover"), ("vacuum", "start"), ("vacuum", "return_to_base")}
    seen = {n: async_mock_service(hass, *n) for n in names}
    for wid in ("pp", "mu", "vu", "co", "op", "va", "vh"):
        await ws.send_json({"type": "tap", "widget": wid})
        await asyncio.sleep(0.03)
    await hass.async_block_till_done()
    assert all(len(c) == 1 for c in seen.values()), {k: len(v) for k, v in seen.items()}
    assert seen[("media_player", "volume_mute")][0].data["is_volume_muted"] is False     # was muted
    await ws.close()


async def test_a_slider_cannot_be_tapped_and_a_label_cannot_be_set(hass, make_entry, hass_client_no_auth):
    ws, _ = await session(hass, make_entry, hass_client_no_auth, [w("label", "light.a", id="l"), w("bar", "light.a", id="b")])
    on = async_mock_service(hass, "light", "turn_on")
    tog = async_mock_service(hass, "homeassistant", "toggle")
    for wid in ("l", "b"):
        for msg in ({"type": "tap"}, {"type": "set", "value": 50}, {"type": "set", "on": True}, {"type": "act", "action": "next"}):
            await ws.send_json({**msg, "widget": wid})
    await asyncio.sleep(0.1)
    assert not on and not tog
    await ws.close()


# ── font size ────────────────────────────────────────────────────────────────

def test_font_size_accepts_only_the_available_sizes():
    assert validate_widgets([w("label", "sensor.x")])[0]["font"] == 0
    for size in (12, 14, 16, 20, 24):
        assert validate_widgets([w("button", "light.a", font=size)])[0]["font"] == size
    for bad in (-1, 10, 13, 100, "big", 16.5, True):
        with pytest.raises(vol.Invalid):
            validate_widgets([w("label", "sensor.x", font=bad)])


def test_the_two_big_digit_fonts_are_for_clocks_only():
    assert validate_widgets([w("clock", font=48)])[0]["font"] == 48
    assert validate_widgets([w("clock", font=32)])[0]["font"] == 32
    for kind, entity in (("label", "sensor.x"), ("button", "light.a"), ("media", "media_player.m"), ("thermostat", "climate.c")):
        with pytest.raises(vol.Invalid):
            validate_widgets([w(kind, entity, font=32)])


async def test_font_goes_to_the_panel_only_when_set(hass, make_entry, hass_client_no_auth):
    ws, (layout, _) = await session(hass, make_entry, hass_client_no_auth, [w("label", "sensor.x", font=24), w("label", "sensor.x", id="w2")])
    first, second = layout["widgets"]
    assert first["font"] == 24 and "font" not in second
    await ws.close()
