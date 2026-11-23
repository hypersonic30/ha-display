"""Firmware updates over the air: what the panel runs, the download endpoint, and the install run."""
import asyncio
import hashlib
import json

import pytest

from homeassistant.config_entries import ConfigEntryState
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.entity_component import DATA_INSTANCES

from custom_components.display import firmware, update
from custom_components.display.const import DOMAIN

from .conftest import FW_LATEST, device


@pytest.fixture
def expected_lingering_timers() -> bool:
    return True


async def setup(hass, make_entry, *devices):
    entry = make_entry(*devices)
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    return entry


async def connect(client_factory, key="key-küche", fw="1.0.0", variant="ili9488"):
    ws = await (await client_factory()).ws_connect("/api/display/ws")
    await ws.send_json({"type": "hello", "key": key, "fw": fw, "variant": variant})
    first = await asyncio.wait_for(ws.receive_json(), 3)
    assert first["type"] == "layout"
    await asyncio.wait_for(ws.receive_json(), 3)           # states
    return ws


def entity_id(hass, entry, name="Küche"):
    sub = next(s for s in entry.subentries.values() if s.title == name)
    return er.async_get(hass).async_get_entity_id("update", DOMAIN, f"{sub.subentry_id}_firmware")


async def settle(hass):
    """Let the websocket messages be handled. No async_block_till_done: a running install keeps it waiting forever."""
    await asyncio.sleep(0.15)


async def test_the_update_entity_compares_what_the_panel_runs_with_the_bundle(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    eid = entity_id(hass, entry)
    assert eid == "update.kuche_firmware" and hass.states.get(eid).state == "unknown"   # nothing connected yet

    old = await connect(hass_client_no_auth, fw="1.0.0")
    await settle(hass)
    state = hass.states.get(eid)
    assert state.state == "on" and state.attributes["installed_version"] == "1.0.0" and state.attributes["latest_version"] == FW_LATEST
    await old.close()

    current = await connect(hass_client_no_auth, fw=FW_LATEST)
    await settle(hass)
    assert hass.states.get(eid).state == "off"
    await current.close()

    odd = await connect(hass_client_no_auth, fw="1.0.0", variant="esp32x")            # a variant this bundle has no image for
    await settle(hass)
    assert hass.states.get(eid).state == "off" and hass.states.get(eid).attributes["latest_version"] == "1.0.0"
    await odd.close()


async def test_the_card_gets_version_variant_and_the_entity(hass, make_entry, hass_client_no_auth, hass_ws_client):
    entry = await setup(hass, make_entry, device("Küche"))
    admin = await hass_ws_client()
    await admin.send_json_auto_id({"type": "display/devices"})
    before = (await admin.receive_json())["result"]["devices"][0]
    assert before["fw"] is None and before["fw_latest"] == FW_LATEST and before["update_entity"] == "update.kuche_firmware"
    assert before["connected_entity"] == "binary_sensor.kuche_connected"
    await connect(hass_client_no_auth, fw="1.2.3", variant="ili9481")
    await admin.send_json_auto_id({"type": "display/devices"})
    after = (await admin.receive_json())["result"]["devices"][0]
    assert after["fw"] == "1.2.3" and after["variant"] == "ili9481"


async def test_what_a_panel_says_about_itself_is_cleaned(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await connect(hass_client_no_auth, fw="<script>alert(1)</script>" + "x" * 80, variant="ili9488")
    info = hass.data[DOMAIN]["store"].info(next(iter(entry.subentries)))
    assert "<" not in info["fw"] and ">" not in info["fw"] and len(info["fw"]) <= 32


async def test_the_firmware_endpoint_serves_only_known_variants_to_known_panels(hass, make_entry, hass_client_no_auth, fake_firmware):
    await setup(hass, make_entry, device("Küche"))
    client = await hass_client_no_auth()
    good = {"X-Device-Key": "key-küche"}

    resp = await client.get("/api/display/firmware/ili9488", headers=good)
    body = await resp.read()
    assert resp.status == 200 and body == fake_firmware["files"]["ili9488"] and int(resp.headers["Content-Length"]) == len(body)
    assert hashlib.md5(body).hexdigest() == fake_firmware["variants"]["ili9488"]["md5"]
    assert (await (await client.get("/api/display/firmware/ili9481", headers=good)).read()) == fake_firmware["files"]["ili9481"]

    for headers in ({}, {"X-Device-Key": "nope"}, {"X-Device-Key": ""}):
        assert (await client.get("/api/display/firmware/ili9488", headers=headers)).status == 401, headers
    for variant in ("nope", "..%2Fmanifest.json", "ILI9488", "a" * 40):
        assert (await client.get(f"/api/display/firmware/{variant}", headers=good)).status in (400, 404), variant   # HA itself refuses ..%2F with 400


async def test_without_a_bundle_there_is_nothing_to_offer(hass, make_entry, hass_client_no_auth, monkeypatch, tmp_path):
    monkeypatch.setattr(firmware, "FIRMWARE_DIR", tmp_path / "empty")
    hass.data[DOMAIN]["firmware"] = await firmware.async_load_bundle(hass)
    entry = await setup(hass, make_entry, device("Küche"))
    assert (await (await hass_client_no_auth()).get("/api/display/firmware/ili9488", headers={"X-Device-Key": "key-küche"})).status == 404
    await connect(hass_client_no_auth, fw="1.0.0")
    await settle(hass)
    assert hass.states.get(entity_id(hass, entry)).state == "off"


async def _install(hass, eid):
    return asyncio.create_task(hass.services.async_call("update", "install", {"entity_id": eid}, blocking=True))


def _entity(hass, eid):
    """The entity object itself: reaches our own checks that Home Assistant's service would stop earlier."""
    return hass.data[DATA_INSTANCES]["update"].get_entity(eid)


async def test_installing_streams_progress_and_finishes_when_the_panel_returns_with_the_new_version(hass, make_entry, hass_client_no_auth, fake_firmware):
    entry = await setup(hass, make_entry, device("Küche"))
    eid = entity_id(hass, entry)
    ws = await connect(hass_client_no_auth, fw="1.0.0")
    task = await _install(hass, eid)

    frame = await asyncio.wait_for(ws.receive_json(), 3)
    info = fake_firmware["variants"]["ili9488"]
    assert frame == {"type": "ota", "version": FW_LATEST, "path": "/api/display/firmware/ili9488", "size": info["size"], "md5": info["md5"]}
    await settle(hass)
    assert hass.states.get(eid).attributes["in_progress"] is True

    await ws.send_json({"type": "ota_status", "state": "downloading", "progress": 40})
    await settle(hass)
    assert hass.states.get(eid).attributes["update_percentage"] == 40
    await ws.send_json({"type": "ota_status", "state": "downloading", "progress": 500})       # clamped, not trusted
    await ws.send_json({"type": "ota_status", "state": "downloading", "progress": True})
    await ws.send_json({"type": "ota_status", "state": "rebooting-into-space", "progress": 70})  # unknown state: ignored
    await settle(hass)
    assert hass.states.get(eid).attributes["update_percentage"] == 0
    await ws.send_json({"type": "ota_status", "state": "done"})
    await settle(hass)
    assert not task.done()                                             # not finished until the panel is back
    await ws.close()
    await settle(hass)
    assert not task.done()

    back = await connect(hass_client_no_auth, fw=FW_LATEST)
    await asyncio.wait_for(task, 3)
    await settle(hass)
    state = hass.states.get(eid)
    assert state.state == "off" and state.attributes["in_progress"] is False and state.attributes["installed_version"] == FW_LATEST
    await back.close()


async def test_a_reported_error_fails_the_install_with_the_panels_words(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    eid = entity_id(hass, entry)
    ws = await connect(hass_client_no_auth)
    task = await _install(hass, eid)
    await asyncio.wait_for(ws.receive_json(), 3)
    await ws.send_json({"type": "ota_status", "state": "error", "error": "md5 <b>mismatch</b>\n" + "y" * 200})
    with pytest.raises(HomeAssistantError) as err:
        await asyncio.wait_for(task, 3)
    assert "mismatch" in str(err.value) and "<" not in str(err.value) and len(str(err.value)) < 140
    await settle(hass)
    assert hass.states.get(eid).attributes["in_progress"] is False
    # and a later attempt is possible again
    again = await _install(hass, eid)
    assert (await asyncio.wait_for(ws.receive_json(), 3))["type"] == "ota"
    again.cancel()


async def test_a_panel_that_fell_back_is_reported_instead_of_hanging(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    eid = entity_id(hass, entry)
    ws = await connect(hass_client_no_auth, fw="1.0.0")
    task = await _install(hass, eid)
    await asyncio.wait_for(ws.receive_json(), 3)
    await ws.send_json({"type": "ota_status", "state": "done"})
    await ws.close()
    await settle(hass)
    await connect(hass_client_no_auth, fw="1.0.0")                      # back on the OLD version
    with pytest.raises(HomeAssistantError, match="old firmware"):
        await asyncio.wait_for(task, 3)


async def test_a_silent_panel_times_out(hass, make_entry, hass_client_no_auth, monkeypatch):
    monkeypatch.setattr(update, "OTA_TIMEOUT", 0.3)
    entry = await setup(hass, make_entry, device("Küche"))
    ws = await connect(hass_client_no_auth)
    task = await _install(hass, entity_id(hass, entry))
    await asyncio.wait_for(ws.receive_json(), 3)
    eid = entity_id(hass, entry)
    with pytest.raises(HomeAssistantError, match="did not report back"):
        await asyncio.wait_for(task, 3)
    await settle(hass)
    state = hass.states.get(eid)                                       # the UI must not keep showing "updating"
    assert state.attributes["in_progress"] is False and state.attributes["update_percentage"] is None


async def test_install_refuses_what_cannot_work(hass, make_entry, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    eid = entity_id(hass, entry)
    entity = _entity(hass, eid)
    with pytest.raises(HomeAssistantError, match="not connected"):
        await entity.async_install(None, False)

    odd = await connect(hass_client_no_auth, variant="esp32x")
    with pytest.raises(HomeAssistantError, match="esp32x"):
        await entity.async_install(None, False)               # (HA's own service would already say "no update available")
    await odd.close()
    await settle(hass)

    ws = await connect(hass_client_no_auth)
    first = asyncio.create_task(entity.async_install(None, False))
    await asyncio.wait_for(ws.receive_json(), 3)
    with pytest.raises(HomeAssistantError, match="already running"):
        await entity.async_install(None, False)
    first.cancel()
    with pytest.raises(asyncio.CancelledError):
        await first


def test_the_shipped_images_match_their_manifest_and_the_firmware_version():
    """Catches a release where the firmware was rebuilt but not packaged (or the other way round)."""
    from pathlib import Path  # noqa: PLC0415

    root = Path(__file__).parent.parent
    bundle = root / "custom_components" / "display" / "firmware"
    manifest = json.loads((bundle / "manifest.json").read_text())
    assert manifest["version"] == (root / "firmware" / "VERSION").read_text().strip()
    assert set(manifest["variants"]) == {"ili9488", "ili9481"}
    for name, info in manifest["variants"].items():
        data = (bundle / info["file"]).read_bytes()
        assert len(data) == info["size"] and hashlib.md5(data).hexdigest() == info["md5"] and hashlib.sha256(data).hexdigest() == info["sha256"], name
        assert manifest["version"].encode() in data and name.encode() in data, f"{name}: version/variant not inside the binary"
        assert (root / "firmware" / "bin" / info["file"]).read_bytes() == data, f"firmware/bin/{info['file']} differs from the shipped image"
