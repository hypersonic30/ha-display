"""Layout storage, validation and live push to the panels."""
from __future__ import annotations

import asyncio
import re
from collections.abc import Callable
from typing import Any

import voluptuous as vol

from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.storage import Store

from .const import (
    ACTION_DOMAINS,
    BUTTON_ACTIONS,
    CLOCK_FORMATS,
    ICONS,
    MIN_SIZE,
    SELECT_DOMAINS,
    SLIDER_DOMAINS,
    TOGGLE_DOMAINS,
    DOMAIN,
    MAX_PAGES,
    MAX_WIDGETS,
    PAGER_MODES,
    SCREEN_H,
    SCREEN_W,
    WIDGET_TYPES,
)

STORAGE_VERSION = 1
STORAGE_KEY = f"{DOMAIN}_layouts"

_ENTITY_ID = re.compile(r"^[a-z0-9_]+\.[a-z0-9_]+$")
_ID = re.compile(r"^[A-Za-z0-9_-]{1,15}$")
MIN_PAGER_W = 60
MIN_PAGER_H = 24


def online_signal(subentry_id: str) -> str:
    return f"{DOMAIN}_online_{subentry_id}"


def info_signal(subentry_id: str) -> str:
    """Firmware version, variant or update progress of a panel changed."""
    return f"{DOMAIN}_info_{subentry_id}"


class OtaRun:
    """One running firmware update of one panel."""

    def __init__(self, target: str, future: asyncio.Future) -> None:
        self.target = target
        self.future = future
        self.state = "starting"
        self.progress = 0
        self.done_seen = False


def empty_layout() -> dict:
    return {"pages": [{"id": "p1", "name": "Seite 1", "widgets": []}], "pager": None, "rev": 0, "screen": None}


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


_COLOR = vol.All(str, vol.Any("", vol.Match(r"^#[0-9a-fA-F]{6}$")), lambda v: v.lower())
_ATTR = vol.All(str, vol.Any("", vol.Match(r"^[a-z0-9_]{1,23}$")))
_THRESHOLD = vol.Any(None, vol.All(vol.Coerce(float), vol.Range(min=-1_000_000, max=1_000_000)))
_NUM = vol.All(vol.Coerce(int), vol.Range(min=-1_000_000, max=1_000_000))
_X = vol.All(vol.Coerce(int), vol.Range(min=0, max=max(SCREEN_W, SCREEN_H) - 1))
_SIZE = vol.All(vol.Coerce(int), vol.Range(min=1, max=max(SCREEN_W, SCREEN_H)))

WIDGET_SCHEMA = vol.Schema(
    {
        vol.Required("id"): vol.All(str, vol.Match(_ID)),
        vol.Required("type"): vol.In(WIDGET_TYPES),
        vol.Optional("entity", default=""): _entity,
        vol.Optional("label", default=""): _text(31),
        vol.Optional("unit", default=""): _text(15),
        vol.Optional("min", default=0): _NUM,
        vol.Optional("max", default=100): _NUM,
        vol.Optional("bg", default=""): _COLOR,
        vol.Optional("fg", default=""): _COLOR,
        vol.Optional("on", default=""): _COLOR,
        vol.Optional("attr", default=""): _ATTR,
        vol.Optional("icon", default=""): vol.In(("",) + ICONS),
        vol.Optional("action", default=""): vol.In(("",) + tuple(sorted({a for v in BUTTON_ACTIONS.values() for a in v}))),
        vol.Optional("format", default="time"): vol.In(CLOCK_FORMATS),
        vol.Optional("warn", default=None): _THRESHOLD,
        vol.Optional("crit", default=None): _THRESHOLD,
        vol.Optional("invert", default=False): bool,
        vol.Required("x"): _X,
        vol.Required("y"): _X,
        vol.Required("w"): vol.All(_SIZE, vol.Range(min=20)),
        vol.Required("h"): vol.All(_SIZE, vol.Range(min=20)),
    },
    extra=vol.REMOVE_EXTRA,
)

PAGER_SCHEMA = vol.Schema(
    {
        vol.Required("x"): _X,
        vol.Required("y"): _X,
        vol.Required("w"): vol.All(_SIZE, vol.Range(min=MIN_PAGER_W)),
        vol.Required("h"): vol.All(_SIZE, vol.Range(min=MIN_PAGER_H)),
        vol.Optional("mode", default="name"): vol.In(PAGER_MODES),
    },
    extra=vol.REMOVE_EXTRA,
)


def _inside(item: dict, screen: tuple[int, int]) -> bool:
    return item["x"] + item["w"] <= screen[0] and item["y"] + item["h"] <= screen[1]


def _widget_problem(widget: dict) -> str | None:
    """Why this widget cannot work as configured (entity domain vs. type/action), or None."""
    kind = widget["type"]
    entity = widget["entity"]
    domain = entity.split(".")[0] if entity else ""
    min_w, min_h = MIN_SIZE.get(kind, (20, 20))
    if widget["w"] < min_w or widget["h"] < min_h:
        return f"a {kind} needs at least {min_w}x{min_h} pixels"
    if kind in ("gauge", "bar", "slider", "arc") and widget["max"] <= widget["min"]:
        return "max must be greater than min"
    needs = {
        "button": ACTION_DOMAINS, "switch": TOGGLE_DOMAINS, "checkbox": TOGGLE_DOMAINS, "slider": SLIDER_DOMAINS,
        "arc": SLIDER_DOMAINS, "dropdown": SELECT_DOMAINS, "roller": SELECT_DOMAINS, "media": {"media_player"},
        "thermostat": {"climate"},
    }.get(kind)
    if needs is not None and entity and domain not in needs:
        return f"a {kind} cannot work with {domain} entities"
    if needs is not None and kind in ("media", "thermostat", "slider", "arc", "dropdown", "roller") and not entity:
        return f"a {kind} needs an entity"
    if kind in ("clock", "panel") and entity:
        return f"a {kind} has no entity"
    action = widget["action"]
    if kind == "button":
        allowed = BUTTON_ACTIONS.get(domain, ())
        if action and action not in allowed:
            return f"{domain or 'this entity'} has no action '{action}'"
        if domain == "climate" and not action:
            return "a climate button needs an action (temp_up or temp_down)"
    elif action:
        return "only buttons have an action"
    return None


def validate_widgets(widgets: Any, screen: tuple[int, int] = (SCREEN_W, SCREEN_H)) -> list[dict]:
    """The cleaned widget list of one page, or vol.Invalid with a readable message."""
    if not isinstance(widgets, list):
        raise vol.Invalid("widgets must be a list")
    if len(widgets) > MAX_WIDGETS:
        raise vol.Invalid(f"at most {MAX_WIDGETS} widgets fit on a page")
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
        if not _inside(widget, screen):
            raise vol.Invalid(f"widget {index}: outside the {screen[0]}x{screen[1]} screen")
        problem = _widget_problem(widget)
        if problem:
            raise vol.Invalid(f"widget {index}: {problem}")
        clean.append(widget)
    return clean


def validate_layout(pages: Any, pager: Any, screen: tuple[int, int]) -> tuple[list[dict], dict | None]:
    """Pages (1..MAX_PAGES, each with its own widgets) and the optional page switcher."""
    if not isinstance(pages, list) or not 1 <= len(pages) <= MAX_PAGES:
        raise vol.Invalid(f"a display has 1 to {MAX_PAGES} pages")
    clean_pages: list[dict] = []
    ids: set[str] = set()
    for number, page in enumerate(pages, start=1):
        if not isinstance(page, dict):
            raise vol.Invalid(f"page {number}: must be an object")
        page_id = page.get("id")
        if not isinstance(page_id, str) or not _ID.match(page_id):
            raise vol.Invalid(f"page {number}: invalid id")
        if page_id in ids:
            raise vol.Invalid(f"page {number}: duplicate id {page_id}")
        ids.add(page_id)
        try:
            name = _text(31)(page.get("name", ""))
        except vol.Invalid as err:
            raise vol.Invalid(f"page {number}: name {err}") from err
        name = name.strip()
        if not name:
            raise vol.Invalid(f"page {number}: needs a name")
        try:
            widgets = validate_widgets(page.get("widgets", []), screen)
        except vol.Invalid as err:
            raise vol.Invalid(f"page '{name}', {err}") from err
        try:
            bg = _COLOR(page.get("bg", ""))
        except vol.Invalid as err:
            raise vol.Invalid(f"page {number}: background {err}") from err
        clean_pages.append({"id": page_id, "name": name, "bg": bg, "widgets": widgets})

    clean_pager = None
    if pager is not None:
        try:
            clean_pager = PAGER_SCHEMA(pager)
        except vol.Invalid as err:
            raise vol.Invalid(f"page switcher: {err}") from err
        if not _inside(clean_pager, screen):
            raise vol.Invalid(f"page switcher: outside the {screen[0]}x{screen[1]} screen")
    return clean_pages, clean_pager


def _fit(item: dict, screen: tuple[int, int]) -> bool:
    """Move/shrink an item onto the screen; True if it changed."""
    before = (item["x"], item["y"], item["w"], item["h"])
    item["w"] = min(item["w"], screen[0])
    item["h"] = min(item["h"], screen[1])
    item["x"] = max(0, min(item["x"], screen[0] - item["w"]))
    item["y"] = max(0, min(item["y"], screen[1] - item["h"]))
    return before != (item["x"], item["y"], item["w"], item["h"])


class LayoutStore:
    """Layouts per device (keyed by subentry id), panel connections and change listeners."""

    def __init__(self, hass: HomeAssistant) -> None:
        self._hass = hass
        self._store: Store = Store(hass, STORAGE_VERSION, STORAGE_KEY)
        self._data: dict[str, dict] = {}
        self._listeners: dict[str, list[Callable[[], None]]] = {}
        self._online: dict[str, set[object]] = {}
        self._info: dict[str, dict] = {}
        self._ota: dict[str, OtaRun] = {}

    async def async_load(self) -> None:
        self._data = await self._store.async_load() or {}
        for key, entry in list(self._data.items()):
            if "pages" not in entry:  # v0.2.x: one flat widget list
                self._data[key] = {
                    "pages": [{"id": "p1", "name": "Seite 1", "widgets": entry.get("widgets", [])}],
                    "pager": None,
                    "rev": entry.get("rev", 0),
                    "screen": None,
                }

    def get(self, device_id: str) -> dict:
        return self._data.get(device_id) or empty_layout()

    async def async_set(self, device_id: str, pages: list[dict], pager: dict | None, screen: tuple[int, int]) -> int:
        rev = self.get(device_id)["rev"] + 1
        self._data[device_id] = {"pages": pages, "pager": pager, "rev": rev, "screen": list(screen)}
        await self._store.async_save(self._data)
        self.notify(device_id)
        return rev

    async def async_fit(self, device_id: str, screen: tuple[int, int]) -> bool:
        """After a rotation: bring everything onto the new screen. True if the layout changed."""
        entry = self._data.get(device_id)
        if entry is None:
            return False
        changed = entry.get("screen") not in (None, list(screen))
        for page in entry["pages"]:
            for widget in page["widgets"]:
                changed |= _fit(widget, screen)
        if entry["pager"]:
            changed |= _fit(entry["pager"], screen)
        if changed or entry.get("screen") is None:
            entry["screen"] = list(screen)
            if changed:
                entry["rev"] += 1
            await self._store.async_save(self._data)
        return changed

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

    # ── what the panel runs, and firmware updates ────────────────────────────

    def info(self, device_id: str) -> dict:
        return self._info.get(device_id) or {"fw": None, "variant": None}

    @callback
    def set_info(self, device_id: str, fw: str | None, variant: str | None) -> None:
        """Called on every hello. A hello with the target version completes a running update."""
        self._info[device_id] = {"fw": fw or None, "variant": variant or None}
        run = self._ota.get(device_id)
        if run and not run.future.done():
            if fw == run.target:
                run.future.set_result(None)
            elif run.done_seen:
                run.future.set_exception(
                    HomeAssistantError("The display restarted but still runs the old firmware (it fell back after the update)")
                )
        async_dispatcher_send(self._hass, info_signal(device_id))

    def ota(self, device_id: str) -> OtaRun | None:
        return self._ota.get(device_id)

    @callback
    def ota_begin(self, device_id: str, target: str) -> asyncio.Future:
        if (run := self._ota.get(device_id)) and not run.future.done():
            raise HomeAssistantError("A firmware update of this display is already running")
        run = OtaRun(target, self._hass.loop.create_future())
        run.future.add_done_callback(lambda f: f.cancelled() or f.exception())  # never "exception was never retrieved"
        self._ota[device_id] = run
        async_dispatcher_send(self._hass, info_signal(device_id))
        return run.future

    @callback
    def ota_status(self, device_id: str, state: str, progress: int, error: str) -> None:
        run = self._ota.get(device_id)
        if run is None or run.future.done():
            return
        run.state = state
        run.progress = progress
        if state == "done":
            run.done_seen = True
        elif state == "error":
            run.future.set_exception(HomeAssistantError(error or "The display reported an update error"))
        async_dispatcher_send(self._hass, info_signal(device_id))

    @callback
    def ota_end(self, device_id: str) -> None:
        self._ota.pop(device_id, None)
        async_dispatcher_send(self._hass, info_signal(device_id))
