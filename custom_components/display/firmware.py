"""The firmware images shipped with the integration, and the endpoint a panel downloads them from."""
from __future__ import annotations

import json
import logging
import re
from pathlib import Path

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.components.http.ban import process_wrong_login
from homeassistant.core import HomeAssistant

from .const import DOMAIN
from .custom_firmware import armed_data
from .devices import find_by_key, get_entry

_LOGGER = logging.getLogger(__name__)

FIRMWARE_DIR = Path(__file__).parent / "firmware"
_VARIANT = re.compile(r"^[a-z0-9]{1,16}$")


def _read_bundle() -> dict | None:
    try:
        manifest = json.loads((FIRMWARE_DIR / "manifest.json").read_text())
    except (OSError, ValueError):
        return None
    variants = {}
    for name, info in manifest.get("variants", {}).items():
        path = FIRMWARE_DIR / info.get("file", "")
        if _VARIANT.match(name) and path.is_file() and path.stat().st_size == info.get("size"):
            variants[name] = {key: info[key] for key in ("file", "size", "md5", "sha256")}
        else:
            _LOGGER.warning("Firmware variant %s is missing or does not match its manifest entry", name)
    if not manifest.get("version") or not variants:
        return None
    return {"version": str(manifest["version"]), "variants": variants}


async def async_load_bundle(hass: HomeAssistant) -> dict | None:
    """{'version': '0.3.0', 'variants': {'ili9488': {file, size, md5, sha256}}} or None without images."""
    return await hass.async_add_executor_job(_read_bundle)


def bundle_of(hass: HomeAssistant) -> dict | None:
    return hass.data[DOMAIN].get("firmware")


class DisplayFirmwareView(HomeAssistantView):
    """GET /api/display/firmware/<variant>, authenticated with the panel's device key."""

    url = "/api/display/firmware/{variant}"
    name = "api:display:firmware"
    requires_auth = False

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def get(self, request: web.Request, variant: str) -> web.StreamResponse:
        key = request.headers.get("X-Device-Key", "")
        entry = get_entry(self.hass)
        sub = find_by_key(entry, key) if key and entry is not None else None
        if sub is None:
            await process_wrong_login(request)
            return web.Response(status=401, text="unknown device key")
        if (custom := armed_data(self.hass, sub.subentry_id)) is not None:   # expert mode: the image picked in the card
            return web.Response(body=custom, headers={"Content-Type": "application/octet-stream", "Cache-Control": "no-store"})
        bundle = bundle_of(self.hass)
        info = bundle["variants"].get(variant) if bundle else None
        if info is None:
            return web.Response(status=404, text="no firmware for this variant")
        return web.FileResponse(FIRMWARE_DIR / info["file"], headers={"Content-Type": "application/octet-stream", "Cache-Control": "no-store"})
