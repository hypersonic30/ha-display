"""Layout storage, validation and live push to the panels."""
from __future__ import annotations

import re
from collections.abc import Callable
from typing import Any

import voluptuous as vol

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.storage import Store

from .const import ACTION_DOMAINS, DOMAIN, MAX_WIDGETS, SCREEN_H, SCREEN_W, WIDGET_TYPES

STORAGE_VERSION = 1
STORAGE_KEY = f"{DOMAIN}_layouts"

_ENTITY_ID = re.compile(r"^[a-z0-9_]+\.[a-z0-9_]+$")
_WIDGET_ID = re.compile(r"^[A-Za-z0-9_-]{1,15}$")


def online_signal(subentry_id: str) -> str:
    return f"{DOMAIN}_online_{subentry_id}"


def _text(max_bytes: int):
    """The firmware keeps these in fixed buffers, so the limit counts UTF-8 bytes."""
    def check(value: Any) -> str:
        if not isinstance(value, str):
            raise vol.Invalid("must be text")
        if len(value.encode()) > max_bytes:
            raise vol.Invalid(f"too long (max {max_bytes} bytes)")
        return value
    return check


def _entity(value: Any) -> str:
    if not isinstance(value, str) or (value and not _ENTITY_ID.match(value)):
        raise vol.Invalid("not an entity id")
    return value


_NUM = vol.All(vol.Coerce(int), vol.Range(min=-1_000_000, max=1_000_000))

WIDGET_SCHEMA = vol.Schema(
    {
        vol.Required("id"): vol.All(str, vol.Match(_WIDGET_ID)),
        vol.Required("type"): vol.In(WIDGET_TYPES),
        vol.Optional("entity", default=""): _entity,
        vol.Optional("label", default=""): _text(31),
        vol.Optional("unit", default=""): _text(15),
        vol.Optional("min", default=0): _NUM,
        vol.Optional("max", default=100): _NUM,
        vol.Required("x"): vol.All(vol.Coerce(int), vol.Range(min=0, max=SCREEN_W - 1)),
        vol.Required("y"): vol.All(vol.Coerce(int), vol.Range(min=0, max=SCREEN_H - 1)),
        vol.Required("w"): vol.All(vol.Coerce(int), vol.Range(min=20, max=SCREEN_W)),
        vol.Required("h"): vol.All(vol.Coerce(int), vol.Range(min=20, max=SCREEN_H)),
    },
    extra=vol.REMOVE_EXTRA,
)


def validate_widgets(widgets: Any) -> list[dict]:
    """Return the cleaned widget list or raise vol.Invalid with a readable message."""
    if not isinstance(widgets, list):
        raise vol.Invalid("widgets must be a list")
    if len(widgets) > MAX_WIDGETS:
        raise vol.Invalid(f"at most {MAX_WIDGETS} widgets fit on the panel")
    clean: list[dict] = []
    seen: set[str] = set()
    for index, raw in enumerate(widgets, start=1):
        try:
            widget = WIDGET_SCHEMA(raw)
        except vol.Invalid as err:
            raise vol.Invalid(f"widget {index}: {err}") from err
        if widget["id"] in seen:
            raise vol.Invalid(f"widget {index}: duplicate id {widget['id']}")
        seen.add(widget["id"])
        if widget["x"] + widget["w"] > SCREEN_W or widget["y"] + widget["h"] > SCREEN_H:
            raise vol.Invalid(f"widget {index}: outside the {SCREEN_W}x{SCREEN_H} screen")
        if widget["type"] in ("button", "switch") and widget["entity"] and widget["entity"].split(".")[0] not in ACTION_DOMAINS:
            raise vol.Invalid(f"widget {index}: a {widget['type']} cannot operate {widget['entity'].split('.')[0]} entities")
        if widget["type"] in ("gauge", "bar") and widget["max"] <= widget["min"]:
            raise vol.Invalid(f"widget {index}: max must be greater than min")
        clean.append(widget)
    return clean


class LayoutStore:
    """Layouts per device (keyed by subentry id), panel connections and change listeners."""

    def __init__(self, hass: HomeAssistant) -> None:
        self._hass = hass
        self._store: Store = Store(hass, STORAGE_VERSION, STORAGE_KEY)
        self._data: dict[str, dict] = {}
        self._listeners: dict[str, list[Callable[[], None]]] = {}
        self._online: dict[str, set[object]] = {}

    async def async_load(self) -> None:
        self._data = await self._store.async_load() or {}

    def get(self, device_id: str) -> dict:
        return self._data.get(device_id) or {"widgets": [], "rev": 0}

    async def async_set(self, device_id: str, widgets: list[dict]) -> int:
        rev = self.get(device_id)["rev"] + 1
        self._data[device_id] = {"widgets": widgets, "rev": rev}
        await self._store.async_save(self._data)
        self.notify(device_id)
        return rev

    async def async_prune(self, keep: set[str]) -> None:
        stale = [key for key in self._data if key not in keep]
        for key in stale:
            del self._data[key]
        if stale:
            await self._store.async_save(self._data)

    async def async_clear(self) -> None:
        self._data = {}
        await self._store.async_remove()

    # ── live push ────────────────────────────────────────────────────────────

    @callback
    def add_listener(self, device_id: str, listener: Callable[[], None]) -> Callable[[], None]:
        self._listeners.setdefault(device_id, []).append(listener)

        @callback
        def remove() -> None:
            listeners = self._listeners.get(device_id, [])
            if listener in listeners:
                listeners.remove(listener)

        return remove

    @callback
    def notify(self, device_id: str) -> None:
        for listener in list(self._listeners.get(device_id, [])):
            listener()

    # ── which panels are connected ───────────────────────────────────────────

    def is_online(self, device_id: str) -> bool:
        return bool(self._online.get(device_id))

    @callback
    def mark_online(self, device_id: str, token: object) -> None:
        self._online.setdefault(device_id, set()).add(token)
        async_dispatcher_send(self._hass, online_signal(device_id))

    @callback
    def mark_offline(self, device_id: str, token: object) -> None:
        self._online.get(device_id, set()).discard(token)
        async_dispatcher_send(self._hass, online_signal(device_id))
