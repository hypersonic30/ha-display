"""Buzzer: the `display.beep` service, the tone limits and the click setting."""
import pytest

from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import device_registry as dr

from custom_components.display import beep
from custom_components.display.const import BEEP_MAX_STEPS, BEEP_MAX_TOTAL_MS, BEEP_PRESETS, DOMAIN

from .conftest import device, make_token
from .test_panel import connect, dev_id, recv, setup


@pytest.fixture
def expected_lingering_timers() -> bool:
    return True


def device_of(hass, entry, name):
    """The device registry id of one display (created with its entities)."""
    wanted = (DOMAIN, dev_id(entry, name))
    return next(d.id for d in dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id) if wanted in d.identifiers)


async def online(hass, make_entry, hass_client_no_auth, *names):
    entry = await setup(hass, make_entry, *[device(n) for n in names])
    ws = await connect(hass_client_no_auth, key=f"key-{names[0].lower()}")
    await recv(ws)   # layout
    await recv(ws)   # states
    return entry, ws


# ── tone building (the clamps) ───────────────────────────────────────────────

def test_presets_are_complete_and_inside_the_limits():
    for name in ("click", "notify", "alert", "doorbell", "success", "error"):
        tones = beep.build_tones(preset=name)
        assert tones == BEEP_PRESETS[name] and tones
        assert beep.clamp_tones(tones) == tones   # nothing in a preset needs clamping


def test_custom_tone_is_clamped():
    assert beep.build_tones(frequency=2200, duration_ms=300) == [[2200, 300]]
    assert beep.build_tones(frequency=1, duration_ms=1) == [[100, 10]]
    assert beep.build_tones(frequency=999999, duration_ms=999999) == [[5000, 2000]]
    assert beep.build_tones() == [[2000, 200]]                      # defaults
    assert beep.build_tones(frequency="x", duration_ms=True) == [[2000, 200]]


def test_preset_wins_over_frequency_and_duration():
    assert beep.build_tones(preset="click", frequency=4000, duration_ms=1500) == BEEP_PRESETS["click"]


def test_repeat_is_limited_in_count_and_total_length():
    three = beep.build_tones(frequency=2000, duration_ms=100, repeat=3)
    assert three == [[2000, 100], [0, 150], [2000, 100], [0, 150], [2000, 100]]
    many = beep.build_tones(frequency=2000, duration_ms=2000, repeat=99)           # 10 x 2 s would be 20 s
    assert sum(ms for _hz, ms in many) == BEEP_MAX_TOTAL_MS and len(many) <= BEEP_MAX_STEPS
    assert beep.build_tones(frequency=2000, duration_ms=10, repeat=0) == [[2000, 10]]
    long_preset = beep.build_tones(preset="alert", repeat=10)
    assert len(long_preset) <= BEEP_MAX_STEPS and sum(ms for _hz, ms in long_preset) <= BEEP_MAX_TOTAL_MS


def test_clamp_tones_drops_empty_steps_and_cuts_the_total():
    assert beep.clamp_tones([[1000, 0], [1000, -5], [1000, 30]]) == [[1000, 30]]
    assert beep.clamp_tones([[7000, 100], [50, 100], [-3, 100]]) == [[5000, 100], [100, 100], [0, 100]]
    cut = beep.clamp_tones([[1000, 2000]] * 4)
    assert cut == [[1000, 2000], [1000, 2000], [1000, 1000]]
    assert len(beep.clamp_tones([[1000, 10]] * 40)) == BEEP_MAX_STEPS


# ── the service ──────────────────────────────────────────────────────────────

async def test_service_sends_the_tones_to_the_connected_panel(hass, make_entry, hass_client_no_auth):
    entry, ws = await online(hass, make_entry, hass_client_no_auth, "Küche")
    await hass.services.async_call(DOMAIN, "beep", {"device_id": device_of(hass, entry, "Küche"), "preset": "doorbell"}, blocking=True)
    assert await recv(ws) == {"type": "beep", "tones": BEEP_PRESETS["doorbell"]}

    await hass.services.async_call(DOMAIN, "beep", {"device_id": device_of(hass, entry, "Küche"), "frequency": 99999, "duration_ms": 99999, "repeat": 2}, blocking=True)
    assert await recv(ws) == {"type": "beep", "tones": [[5000, 2000], [0, 150], [5000, 2000]]}


async def test_service_accepts_an_entity_of_the_display(hass, make_entry, hass_client_no_auth):
    entry, ws = await online(hass, make_entry, hass_client_no_auth, "Küche")
    await hass.services.async_call(DOMAIN, "beep", {"entity_id": "binary_sensor.kuche_connected", "preset": "click"}, blocking=True)
    assert await recv(ws) == {"type": "beep", "tones": BEEP_PRESETS["click"]}


async def test_service_fails_when_the_display_is_offline(hass, make_entry):
    entry = await setup(hass, make_entry, device("Küche"))
    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(DOMAIN, "beep", {"device_id": device_of(hass, entry, "Küche"), "preset": "alert"}, blocking=True)
    assert err.value.translation_key == "display_offline"


async def test_nobody_beeps_when_one_of_two_targets_is_offline(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    ws = await connect(hass_client_no_auth, key="key-küche")
    await recv(ws); await recv(ws)
    ids = [device_of(hass, entry, "Küche"), device_of(hass, entry, "Bad")]
    with pytest.raises(ServiceValidationError):
        await hass.services.async_call(DOMAIN, "beep", {"device_id": ids, "preset": "alert"}, blocking=True)
    # Küche is online and must not have been beeped: the next frame the panel gets is the one we send now
    await hass.services.async_call(DOMAIN, "beep", {"device_id": ids[0], "preset": "click"}, blocking=True)
    assert await recv(ws) == {"type": "beep", "tones": BEEP_PRESETS["click"]}


async def test_service_refuses_foreign_devices_and_no_target(hass, make_entry, hass_client_no_auth):
    from pytest_homeassistant_custom_component.common import MockConfigEntry  # noqa: PLC0415

    entry, ws = await online(hass, make_entry, hass_client_no_auth, "Küche")
    other = MockConfigEntry(domain="test", title="Other")
    other.add_to_hass(hass)
    foreign = dr.async_get(hass).async_get_or_create(config_entry_id=other.entry_id, identifiers={("test", "x")})
    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(DOMAIN, "beep", {"device_id": foreign.id, "preset": "alert"}, blocking=True)
    assert err.value.translation_key == "not_a_display"
    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(DOMAIN, "beep", {"device_id": "does-not-exist", "preset": "alert"}, blocking=True)
    assert err.value.translation_key == "not_a_display"
    with pytest.raises(ServiceValidationError) as err:
        await hass.services.async_call(DOMAIN, "beep", {"preset": "alert"}, blocking=True)
    assert err.value.translation_key == "no_target"
    # nothing reached the panel: the next frame is the one we ask for now
    await hass.services.async_call(DOMAIN, "beep", {"device_id": device_of(hass, entry, "Küche"), "preset": "success"}, blocking=True)
    assert await recv(ws) == {"type": "beep", "tones": BEEP_PRESETS["success"]}


async def test_unknown_preset_is_refused_by_the_schema(hass, make_entry, hass_client_no_auth):
    import voluptuous as vol  # noqa: PLC0415

    entry, _ws = await online(hass, make_entry, hass_client_no_auth, "Küche")
    with pytest.raises(vol.Invalid):
        await hass.services.async_call(DOMAIN, "beep", {"device_id": device_of(hass, entry, "Küche"), "preset": "siren"}, blocking=True)


# ── the click setting ────────────────────────────────────────────────────────

async def test_click_setting_reaches_the_panel_and_survives_a_reconfigure(hass, make_entry, hass_client_no_auth, hass_ws_client):
    entry, ws = await online(hass, make_entry, hass_client_no_auth, "Küche")
    first = hass.data[DOMAIN]["store"]
    sub_id = dev_id(entry, "Küche")
    admin = await hass_ws_client()

    await admin.send_json_auto_id({"type": "display/settings/set", "device_id": sub_id, "click": True})
    reply = await admin.receive_json()
    assert reply["success"] and reply["result"]["settings"]["click"] is True
    await hass.async_block_till_done()
    assert (await recv(ws))["settings"]["click"] is True       # pushed to the connected panel

    await admin.send_json_auto_id({"type": "display/settings/set", "device_id": sub_id, "click": False})
    assert (await admin.receive_json())["result"]["settings"]["click"] is False
    assert first is hass.data[DOMAIN]["store"]


async def test_click_setting_needs_an_administrator(hass, make_entry, hass_ws_client):
    entry = await setup(hass, make_entry, device("Küche"))
    user = await hass_ws_client(hass, await make_token(hass, name="reader"))
    await user.send_json_auto_id({"type": "display/settings/set", "device_id": dev_id(entry, "Küche"), "click": True})
    reply = await user.receive_json()
    assert not reply["success"] and reply["error"]["code"] == "unauthorized"


async def test_click_setting_for_an_unknown_display(hass, make_entry, hass_ws_client):
    await setup(hass, make_entry, device("Küche"))
    admin = await hass_ws_client()
    await admin.send_json_auto_id({"type": "display/settings/set", "device_id": "nope", "click": True})
    assert (await admin.receive_json())["error"]["code"] == "not_found"
