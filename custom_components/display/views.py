from __future__ import annotations

import json
import voluptuous as vol
from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store

from .const import DOMAIN

STORAGE_KEY = f"{DOMAIN}_layouts"
STORAGE_VERSION = 1

_WIDGET_TYPES = {"label", "button", "gauge", "switch", "bar", "clock"}


def _validate_widget(w: dict) -> str | None:
    """Return error string or None."""
    if w.get("type") not in _WIDGET_TYPES:
        return f"unknown widget type: {w.get('type')}"
    for field in ("id", "x", "y", "w", "h"):
        if field not in w:
            return f"widget missing field: {field}"
    return None


class LayoutStore:
    """Thin wrapper around HA Storage for layout persistence."""

    def __init__(self, hass: HomeAssistant) -> None:
        self._store: Store = Store(hass, STORAGE_VERSION, STORAGE_KEY)
        self._data: dict[str, dict] = {}

    async def async_load(self) -> None:
        loaded = await self._store.async_load()
        self._data = loaded or {}

    async def async_save(self) -> None:
        await self._store.async_save(self._data)

    def get(self, device: str) -> dict | None:
        return self._data.get(device)

    def all_devices(self) -> list[str]:
        return list(self._data.keys())

    async def async_set(self, device: str, layout: dict) -> None:
        self._data[device] = layout
        await self.async_save()

    async def async_delete(self, device: str) -> bool:
        if device not in self._data:
            return False
        del self._data[device]
        await self.async_save()
        return True


# ── GET/POST /api/display/layout/{device_name} ────────────────────────────────

class DisplayLayoutView(HomeAssistantView):
    url  = "/api/display/layout/{device_name}"
    name = "api:display:layout"
    requires_auth = True

    def __init__(self, store: LayoutStore) -> None:
        self._store = store

    async def get(self, request: web.Request, device_name: str) -> web.Response:
        layout = self._store.get(device_name)
        if layout is None:
            return self.json_message(f"No layout for device '{device_name}'",
                                     status_code=404)
        return self.json(layout)

    async def post(self, request: web.Request, device_name: str) -> web.Response:
        try:
            body = await request.json()
        except (json.JSONDecodeError, ValueError):
            return self.json_message("Invalid JSON", status_code=400)

        widgets = body.get("widgets")
        if not isinstance(widgets, list):
            return self.json_message("'widgets' must be a list", status_code=400)

        for w in widgets:
            err = _validate_widget(w)
            if err:
                return self.json_message(err, status_code=400)

        layout = {"device": device_name, "widgets": widgets}
        await self._store.async_set(device_name, layout)
        return self.json({"ok": True, "device": device_name,
                          "widget_count": len(widgets)})

    async def delete(self, request: web.Request, device_name: str) -> web.Response:
        deleted = await self._store.async_delete(device_name)
        if not deleted:
            return self.json_message(f"No layout for '{device_name}'",
                                     status_code=404)
        return self.json({"ok": True})


# ── GET /api/display/devices ──────────────────────────────────────────────────

class DisplayDevicesView(HomeAssistantView):
    url  = "/api/display/devices"
    name = "api:display:devices"
    requires_auth = True

    def __init__(self, store: LayoutStore) -> None:
        self._store = store

    async def get(self, request: web.Request) -> web.Response:
        return self.json({"devices": self._store.all_devices()})
