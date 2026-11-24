"""Expert mode: a firmware image the user picked in the card, installed over the air.

The image is checked, kept in memory (never on disk) and handed to the panel at the same download address the
bundled images use: the firmware only accepts that path, so a panel never needs to know the difference. It is
served only while an installation of exactly this image runs ("armed"), and only to the panel it was meant for."""
from __future__ import annotations

import asyncio
import hashlib
import re
from typing import Any

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError

from .const import DOMAIN
from .devices import get_entry

MAX_SIZE = 0x1E0000       # the OTA partitions of firmware/min_spiffs.csv
MIN_SIZE = 64 * 1024      # anything smaller is not an Arduino build
OTA_TIMEOUT = 300         # seconds from "go" until the panel is back
_IMAGE_MAGIC = 0xE9
_APP_DESC_MAGIC = b"\x32\x54\xcd\xab"
_APP_DESC_AT = 32         # 24 byte image header + 8 byte segment header
_NAME = re.compile(r"[^\w .()+-]")


class ImageError(ValueError):
    """The picked file is not an image this display can run. `code` is a stable key for the card."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def _text(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")[:32]


def inspect(data: bytes) -> dict[str, Any]:
    """Size, checksums and (for information only) version and project name of an ESP32 application image."""
    if len(data) > MAX_SIZE:
        raise ImageError("too_big", f"The file is {len(data)} bytes, the display has room for {MAX_SIZE}")
    if len(data) < MIN_SIZE:
        raise ImageError("too_small", "The file is too small for a firmware image")
    if data[0] != _IMAGE_MAGIC or not 1 <= data[1] <= 16:
        raise ImageError("not_image", "This is not an ESP32 firmware image (use firmware.bin, not bootloader.bin or a merged image)")
    if int.from_bytes(data[12:14], "little") != 0:
        raise ImageError("wrong_chip", "This image is built for another chip than the ESP32")
    info: dict[str, Any] = {"size": len(data), "md5": hashlib.md5(data).hexdigest(), "sha256": hashlib.sha256(data).hexdigest(),
                            "version": "", "project": ""}
    if data[_APP_DESC_AT:_APP_DESC_AT + 4] == _APP_DESC_MAGIC:
        info["version"] = _text(data[_APP_DESC_AT + 16:_APP_DESC_AT + 48])
        info["project"] = _text(data[_APP_DESC_AT + 48:_APP_DESC_AT + 80])
    return info


def clean_name(name: str | None) -> str:
    return _NAME.sub("", name or "")[:60].strip()


class Pending:
    """The image picked for one display."""

    def __init__(self, data: bytes, info: dict[str, Any], name: str) -> None:
        self.data = data
        self.info = info
        self.name = name
        self.armed = False

    def public(self) -> dict[str, Any]:
        return {"name": self.name, **self.info}


def _pending(hass: HomeAssistant) -> dict[str, Pending]:
    return hass.data[DOMAIN].setdefault("custom_fw", {})


def pending_for(hass: HomeAssistant, device_id: str) -> Pending | None:
    return _pending(hass).get(device_id)


def armed_data(hass: HomeAssistant, device_id: str) -> bytes | None:
    """The image to serve to this display, only while its installation runs."""
    pending = _pending(hass).get(device_id)
    return pending.data if pending and pending.armed else None


class CustomFirmwareView(HomeAssistantView):
    """POST /api/display/custom-firmware/<device_id>?name=firmware.bin, body = the raw image. Administrators only."""

    url = "/api/display/custom-firmware/{device_id}"
    name = "api:display:custom_firmware"
    requires_auth = True

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def post(self, request: web.Request, device_id: str) -> web.Response:
        user = request.get("hass_user")
        if user is None or not user.is_admin:
            return self.json_message("Administrator rights are needed", status_code=403)
        entry = get_entry(self.hass)
        if entry is None or device_id not in entry.subentries:
            return self.json_message("Unknown display", status_code=404)
        if (request.content_length or 0) > MAX_SIZE:
            return self.json({"code": "too_big", "message": "The file is too big"}, status_code=413)
        data = await request.read()
        try:
            info = inspect(data)
        except ImageError as err:
            return self.json({"code": err.code, "message": str(err)}, status_code=400)
        current = _pending(self.hass).get(device_id)
        if current and current.armed:
            return self.json({"code": "busy", "message": "An installation of this display is running"}, status_code=409)
        pending = Pending(data, info, clean_name(request.query.get("name")) or "firmware.bin")
        _pending(self.hass)[device_id] = pending
        return self.json(pending.public())


def session_of(hass: HomeAssistant, device_id: str):
    return next((s for s in hass.data[DOMAIN]["sessions"] if s.device_id == device_id), None)


async def async_install(hass: HomeAssistant, device_id: str) -> dict[str, Any]:
    """Send the picked image to the display and wait until it runs it. Raises HomeAssistantError with a readable reason."""
    pending = pending_for(hass, device_id)
    if pending is None:
        raise HomeAssistantError("No firmware file was picked for this display")
    session = session_of(hass, device_id)
    if session is None:
        raise HomeAssistantError("The display is not connected - switch it on and try again")
    store = hass.data[DOMAIN]["store"]
    future = store.ota_begin(device_id, None)           # custom: done when the panel reports "done" and comes back
    pending.armed = True
    try:
        await session.start_ota(pending.info["version"] or "custom", pending.info)
        await asyncio.wait_for(future, OTA_TIMEOUT)
    except TimeoutError as err:
        raise HomeAssistantError("The display did not report back after the update - check it, it may have fallen back") from err
    finally:
        pending.armed = False
        _pending(hass).pop(device_id, None)
        store.ota_end(device_id)
    return {"fw": store.info(device_id)["fw"]}
