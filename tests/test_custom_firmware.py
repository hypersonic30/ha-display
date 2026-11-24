"""Expert mode: a firmware file picked in the card is checked, uploaded and installed over the air."""
import asyncio
import hashlib

import pytest

from homeassistant.exceptions import HomeAssistantError

from custom_components.display import custom_firmware
from custom_components.display.const import DOMAIN

from .conftest import device, make_token
from .test_ota import connect, settle, setup


def image(version="0.9.9-test", project="ha-display", size=200_000, chip=0, magic=0xE9, desc=True) -> bytes:
    head = bytearray(24)
    head[0], head[1] = magic, 3
    head[12:14] = chip.to_bytes(2, "little")
    body = bytearray(8)                                   # segment header
    if desc:
        d = bytearray(80)
        d[0:4] = b"\x32\x54\xcd\xab"
        d[16:16 + len(version)] = version.encode()
        d[48:48 + len(project)] = project.encode()
        body += d
    data = bytes(head) + bytes(body)
    return data + b"\x5a" * (size - len(data))


def device_id(entry):
    return next(iter(entry.subentries))


async def upload(client, entry, data, name="firmware.bin"):
    return await client.post(f"/api/display/custom-firmware/{device_id(entry)}?name={name}", data=data)


def test_inspect_reads_size_checksums_and_the_names_in_the_image():
    data = image()
    info = custom_firmware.inspect(data)
    assert info == {"size": len(data), "md5": hashlib.md5(data).hexdigest(), "sha256": hashlib.sha256(data).hexdigest(),
                    "version": "0.9.9-test", "project": "ha-display"}
    assert custom_firmware.inspect(image(desc=False))["version"] == ""


@pytest.mark.parametrize(
    ("data", "code"),
    [
        (image(size=custom_firmware.MAX_SIZE + 1), "too_big"),
        (image(size=1000), "too_small"),
        (image(magic=0x00), "not_image"),
        (b"\xe9\x00" + b"\0" * 100_000, "not_image"),       # no segments
        (image(chip=9), "wrong_chip"),
    ],
)
def test_inspect_refuses_what_cannot_run(data, code):
    with pytest.raises(custom_firmware.ImageError) as err:
        custom_firmware.inspect(data)
    assert err.value.code == code


async def test_an_administrator_can_upload_and_the_card_can_ask_what_is_waiting(hass, make_entry, hass_client, hass_ws_client):
    entry = await setup(hass, make_entry, device("Küche"))
    client = await hass_client()
    data = image()
    resp = await upload(client, entry, data, "my test (1).bin")
    body = await resp.json()
    assert resp.status == 200 and body["name"] == "my test (1).bin" and body["version"] == "0.9.9-test" and body["size"] == len(data)

    ws = await hass_ws_client()
    await ws.send_json_auto_id({"type": "display/custom_firmware/get", "device_id": device_id(entry)})
    assert (await ws.receive_json())["result"] == body
    await ws.send_json_auto_id({"type": "display/custom_firmware/discard", "device_id": device_id(entry)})
    assert (await ws.receive_json())["success"]
    await ws.send_json_auto_id({"type": "display/custom_firmware/get", "device_id": device_id(entry)})
    assert (await ws.receive_json())["result"] is None


async def test_upload_refuses_strangers_bad_files_and_unknown_displays(hass, make_entry, hass_client, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    user = await make_token(hass, admin=False, long_lived=True, name="guest")
    anon = await hass_client_no_auth()
    assert (await upload(anon, entry, image())).status == 401
    plain = await hass_client_no_auth()
    resp = await plain.post(f"/api/display/custom-firmware/{device_id(entry)}", data=image(), headers={"Authorization": f"Bearer {user}"})
    assert resp.status == 403
    assert custom_firmware.pending_for(hass, device_id(entry)) is None

    admin = await hass_client()
    bad = await upload(admin, entry, b"hello world" * 10_000)
    assert bad.status == 400 and (await bad.json())["code"] == "not_image"
    assert (await admin.post("/api/display/custom-firmware/nope", data=image())).status == 404
    assert custom_firmware.pending_for(hass, device_id(entry)) is None


async def test_the_image_is_served_only_to_the_right_panel_and_only_while_it_installs(hass, make_entry, hass_client, hass_client_no_auth, fake_firmware):
    entry = await setup(hass, make_entry, device("Küche"), device("Bad"))
    data = image()
    await upload(await hass_client(), entry, data)
    panel = await hass_client_no_auth()
    kitchen, bath = {"X-Device-Key": "key-küche"}, {"X-Device-Key": "key-bad"}
    # picked but not started: a panel still gets the bundled image
    assert (await (await panel.get("/api/display/firmware/ili9488", headers=kitchen)).read()) == fake_firmware["files"]["ili9488"]

    ws = await connect(hass_client_no_auth)
    task = asyncio.create_task(custom_firmware.async_install(hass, device_id(entry)))
    frame = await asyncio.wait_for(ws.receive_json(), 3)
    info = custom_firmware.inspect(data)
    assert frame == {"type": "ota", "version": "0.9.9-test", "path": "/api/display/firmware/ili9488", "size": len(data), "md5": info["md5"]}
    assert (await (await panel.get("/api/display/firmware/ili9488", headers=kitchen)).read()) == data
    assert (await (await panel.get("/api/display/firmware/ili9488", headers=bath)).read()) == fake_firmware["files"]["ili9488"]   # another panel: bundled

    await ws.send_json({"type": "ota_status", "state": "done"})
    await ws.close()
    await settle(hass)
    assert not task.done()
    back = await connect(hass_client_no_auth, fw="whatever-it-reports")
    result = await asyncio.wait_for(task, 3)
    assert result == {"fw": "whatever-it-reports"}
    assert custom_firmware.pending_for(hass, device_id(entry)) is None          # gone after the run
    assert (await (await panel.get("/api/display/firmware/ili9488", headers=kitchen)).read()) == fake_firmware["files"]["ili9488"]
    await back.close()


async def test_a_reconnect_during_the_download_does_not_finish_the_install(hass, make_entry, hass_client, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await upload(await hass_client(), entry, image())
    ws = await connect(hass_client_no_auth)
    task = asyncio.create_task(custom_firmware.async_install(hass, device_id(entry)))
    await asyncio.wait_for(ws.receive_json(), 3)
    await ws.close()
    await settle(hass)
    await connect(hass_client_no_auth)                       # back without ever reporting "done"
    await settle(hass)
    assert not task.done()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task


async def test_a_reported_error_ends_the_run_and_forgets_the_file(hass, make_entry, hass_client, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    await upload(await hass_client(), entry, image())
    ws = await connect(hass_client_no_auth)
    task = asyncio.create_task(custom_firmware.async_install(hass, device_id(entry)))
    await asyncio.wait_for(ws.receive_json(), 3)
    await ws.send_json({"type": "ota_status", "state": "error", "error": "md5 mismatch"})
    with pytest.raises(HomeAssistantError, match="mismatch"):
        await asyncio.wait_for(task, 3)
    assert custom_firmware.pending_for(hass, device_id(entry)) is None
    assert hass.data[DOMAIN]["store"].ota(device_id(entry)) is None


async def test_install_over_the_card_command_and_its_refusals(hass, make_entry, hass_client, hass_client_no_auth, hass_ws_client):
    entry = await setup(hass, make_entry, device("Küche"))
    ws = await hass_ws_client()
    msg = {"type": "display/custom_firmware/install", "device_id": device_id(entry)}
    await ws.send_json_auto_id(msg)
    assert "No firmware file" in (await ws.receive_json())["error"]["message"]

    await upload(await hass_client(), entry, image())
    await ws.send_json_auto_id(msg)
    assert "not connected" in (await ws.receive_json())["error"]["message"]
    assert custom_firmware.pending_for(hass, device_id(entry)) is not None        # kept: the user can switch the display on and retry

    await ws.send_json_auto_id({"type": "display/custom_firmware/install", "device_id": "nope"})
    assert (await ws.receive_json())["error"]["code"] == "not_found"


async def test_a_second_run_is_refused_and_a_running_one_keeps_its_file(hass, make_entry, hass_client, hass_client_no_auth):
    entry = await setup(hass, make_entry, device("Küche"))
    admin = await hass_client()
    await upload(admin, entry, image())
    ws = await connect(hass_client_no_auth)
    first = asyncio.create_task(custom_firmware.async_install(hass, device_id(entry)))
    await asyncio.wait_for(ws.receive_json(), 3)
    with pytest.raises(HomeAssistantError, match="already running"):
        await custom_firmware.async_install(hass, device_id(entry))
    assert custom_firmware.pending_for(hass, device_id(entry)).armed                 # the failed second call did not disarm it
    assert (await upload(admin, entry, image(version="other"))).status == 409          # no swapping while it is served
    first.cancel()
    with pytest.raises(asyncio.CancelledError):
        await first
