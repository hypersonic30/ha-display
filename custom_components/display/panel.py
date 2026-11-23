"""The panels' own endpoint: display -> integration -> Home Assistant.

A panel never holds a Home Assistant token. It proves who it is with its device key, receives its layout and the
states of the entities in it, and reports taps. What a tap does is decided here, from the stored layout, so a panel
can only operate the widgets it was given.
"""
from __future__ import annotations

import asyncio
import logging
import re
import time
from datetime import timedelta
from typing import Any

from aiohttp import WSMsgType, web

from homeassistant.components.http import HomeAssistantView
from homeassistant.components.http.ban import process_wrong_login
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event, async_track_time_interval
from homeassistant.util import dt as dt_util

from .const import (
    ACTION_DOMAINS,
    BUTTON_ACTIONS,
    DEFAULT_ACTION,
    DOMAIN,
    MEDIA_ACTIONS,
    OPTION_BYTES,
    OPTIONS_MAX,
    THERMOSTAT_ACTIONS,
)
from .devices import brightness_of, find_by_key, get_entry, rotation_of, screen_size

_LOGGER = logging.getLogger(__name__)

HELLO_TIMEOUT = 10
MAX_STATE_BYTES = 47          # the firmware keeps a state in a 48 byte buffer
MIN_ACTION_GAP = 0.1          # seconds between taps of one panel
CLOSE_AUTH = 4001
CLOSE_GONE = 4002
_TEXT = re.compile(r"[^0-9A-Za-z._+ -]")
OTA_STATES = ("downloading", "done", "error")


def _clean(value: Any, limit: int) -> str:
    """Text from a panel: short, plain characters only (it ends up in the UI and in logs)."""
    return _TEXT.sub("", value)[:limit] if isinstance(value, str) else ""


def _short(state: str) -> str:
    return state.encode()[:MAX_STATE_BYTES].decode(errors="ignore")


# Wire format: a widget only carries the fields that differ from these defaults.
DEFAULTS = {
    "entity": "", "label": "", "unit": "", "bg": "", "fg": "", "on": "", "attr": "", "icon": "", "action": "",
    "format": "time", "warn": None, "crit": None, "invert": False,
}
TIME_EVERY = timedelta(minutes=5)
VALUE_GAP = 0.05              # seconds between slider values (the last one must always get through)
PERCENT_DOMAINS = ("light", "cover", "fan", "media_player")


def _wire(widget: dict) -> dict:
    return {key: value for key, value in widget.items() if key not in DEFAULTS or value != DEFAULTS[key]}


def _fmt(value: Any) -> str:
    if isinstance(value, bool):
        return "on" if value else "off"
    if isinstance(value, float):
        return f"{value:.1f}".rstrip("0").rstrip(".")
    if isinstance(value, (list, tuple)):
        return ", ".join(_fmt(v) for v in value)
    return str(value)


def _sources(widget: dict) -> list[tuple[str, str]]:
    """(key, name) pairs the panel reads for this widget; name '' is the state, others are derived values."""
    entity = widget["entity"]
    if not entity:
        return []
    kind = widget["type"]
    if kind in ("slider", "arc"):
        return [(f"{entity}|value", "value")]
    if kind in ("dropdown", "roller"):
        return [(f"{entity}|option", "option")]
    if kind == "media":
        return [(entity, ""), (f"{entity}|media_title", "media_title"), (f"{entity}|media_artist", "media_artist"), (f"{entity}|value", "value")]
    if kind == "thermostat":
        return [(entity, ""), (f"{entity}|current_temperature", "current_temperature"), (f"{entity}|temperature", "temperature")]
    sources = [(entity, "")]
    if widget.get("attr"):
        sources.append((f"{entity}|{widget['attr']}", widget["attr"]))
    return sources


def _derived(widget: dict, name: str, state: Any) -> str:
    """The text a panel gets for one key of this widget."""
    if state is None:
        return "unavailable" if name == "" else ""
    if name == "":
        return _short(state.state)
    domain = widget["entity"].split(".")[0]
    attrs = state.attributes
    if name == "option":
        return _short(str(attrs.get("source") or "") if domain == "media_player" else state.state)
    if name == "value":
        if domain == "light":
            level = attrs.get("brightness")
            return str(round(level * 100 / 255)) if state.state == "on" and level is not None else "0"
        if domain == "cover":
            return str(attrs.get("current_position") or 0)
        if domain == "fan":
            return str(attrs.get("percentage") or 0)
        if domain == "media_player":
            level = attrs.get("volume_level")
            return str(round(level * 100)) if isinstance(level, (int, float)) else "0"
        return _short(state.state) if _is_number(state.state) else "0"
    value = attrs.get(name)
    return "" if value is None else _short(_fmt(value))


def _is_number(text: str) -> bool:
    try:
        float(text)
    except ValueError:
        return False
    return True


def _number(value: Any) -> float | None:
    return None if isinstance(value, bool) or not isinstance(value, (int, float)) or value != value else float(value)


def _option_list(widget: dict, state: Any) -> list[str]:
    if state is None:
        return []
    raw = state.attributes.get("source_list" if widget["entity"].startswith("media_player.") else "options") or []
    return [str(o) for o in raw if str(o)][:OPTIONS_MAX]


def _short_option(option: str) -> str:
    return option.encode()[:OPTION_BYTES].decode(errors="ignore")


class PanelSession:
    """One connected, authenticated panel."""

    def __init__(self, hass: HomeAssistant, ws: web.WebSocketResponse, device_id: str, key: str, fw: str = "", variant: str = "") -> None:
        self.hass = hass
        self.ws = ws
        self.device_id = device_id
        self.key = key
        self.fw = fw
        self.variant = variant
        self._store = hass.data[DOMAIN]["store"]
        self._lock = asyncio.Lock()
        self._widgets: list[dict] = []
        self._by_entity: dict[str, list[tuple[dict, str, str]]] = {}   # entity -> [(widget, key, name)]
        self._options: dict[str, dict[str, str]] = {}                  # widget id -> {shown option: real option}
        self._sent_options: dict[str, list[str]] = {}
        self._page = 0
        self._unsub_states = None
        self._unsub_time = None
        self._last_action = 0.0
        self._last_page = 0.0

    async def _send(self, payload: dict[str, Any]) -> None:
        async with self._lock:
            if not self.ws.closed:
                await self.ws.send_json(payload)

    def _layout_payload(self) -> dict | None:
        entry = get_entry(self.hass)
        sub = entry.subentries.get(self.device_id) if entry else None
        if sub is None:
            return None
        layout = self._store.get(self.device_id)
        pages = layout["pages"]
        self._page = max(0, min(self._page, len(pages) - 1))
        page = pages[self._page]
        width, height = screen_size(rotation_of(sub))
        info = {"index": self._page, "count": len(pages), "name": page["name"]}
        if page.get("bg"):
            info["bg"] = page["bg"]
        return {
            "type": "layout",
            "name": sub.title,
            "rev": layout["rev"],
            "settings": {"brightness": brightness_of(sub), "rotation": rotation_of(sub)},
            "screen": {"w": width, "h": height},
            "page": info,
            # Only worth showing when there is somewhere to go.
            "pager": layout["pager"] if len(pages) > 1 else None,
            "widgets": [_wire(w) for w in page["widgets"]],
        }

    def _values(self, entity_id: str) -> dict[str, str]:
        state = self.hass.states.get(entity_id)
        return {key: _derived(widget, name, state) for widget, key, name in self._by_entity.get(entity_id, [])}

    async def send_layout(self) -> None:
        payload = self._layout_payload()
        if payload is None:
            await self.close(CLOSE_GONE, "gone")
            return
        layout = self._store.get(self.device_id)
        self._widgets = layout["pages"][self._page]["widgets"]
        self._by_entity = {}
        for widget in self._widgets:
            for key, name in _sources(widget):
                self._by_entity.setdefault(widget["entity"], []).append((widget, key, name))
        self._options = {}
        self._sent_options = {}
        entities = sorted(self._by_entity)
        if self._unsub_states:
            self._unsub_states()
            self._unsub_states = None
        if entities:
            self._unsub_states = async_track_state_change_event(self.hass, entities, self._on_state)
        await self._send(payload)
        states: dict[str, str] = {}
        for entity in entities:
            states.update(self._values(entity))
        await self._send({"type": "states", "states": states})
        for entity in entities:
            await self._send_options(entity)
        if self._has_clock():
            await self.send_time()

    def _has_clock(self) -> bool:
        return any(w["type"] == "clock" for w in self._widgets)

    async def send_time(self) -> None:
        now = dt_util.now()
        offset = now.utcoffset()
        await self._send({"type": "time", "epoch": int(now.timestamp()), "offset": int(offset.total_seconds()) if offset else 0})

    @callback
    def _on_time(self, _now: Any) -> None:
        if self._has_clock():
            self.hass.async_create_task(self.send_time())

    def start(self) -> None:
        self._unsub_time = async_track_time_interval(self.hass, self._on_time, TIME_EVERY)

    async def _send_options(self, entity_id: str, only_changed: bool = False) -> None:
        state = self.hass.states.get(entity_id)
        for widget, _key, name in self._by_entity.get(entity_id, []):
            if widget["type"] not in ("dropdown", "roller") or name != "option":
                continue
            real = _option_list(widget, state)
            shown = [_short_option(o) for o in real]
            if only_changed and self._sent_options.get(widget["id"]) == shown:
                continue
            self._sent_options[widget["id"]] = shown
            self._options[widget["id"]] = dict(zip(shown, real))
            await self._send({"type": "options", "widget": widget["id"], "items": shown})

    @callback
    def layout_changed(self) -> None:
        self.hass.async_create_task(self.send_layout())

    @callback
    def _on_state(self, event: Event[EventStateChangedData]) -> None:
        entity = event.data["entity_id"]
        self.hass.async_create_task(self._push_state(entity))

    async def _push_state(self, entity: str) -> None:
        await self._send({"type": "states", "states": self._values(entity)})
        await self._send_options(entity, only_changed=True)

    async def close(self, code: int = 1000, reason: str = "") -> None:
        if not self.ws.closed:
            await self.ws.close(code=code, message=reason.encode())

    async def _switch_page(self, direction: Any) -> None:
        now = time.monotonic()
        if direction not in ("next", "prev") or now - self._last_page < MIN_ACTION_GAP:
            return
        self._last_page = now
        count = len(self._store.get(self.device_id)["pages"])
        self._page = (self._page + (1 if direction == "next" else -1)) % count
        await self.send_layout()

    async def start_ota(self, version: str, info: dict) -> None:
        await self._send(
            {"type": "ota", "version": version, "path": f"/api/display/firmware/{self.variant}", "size": info["size"], "md5": info["md5"]}
        )

    def _ota_status(self, msg: dict) -> None:
        state = msg.get("state")
        progress = msg.get("progress")
        if state not in OTA_STATES:
            return
        progress = max(0, min(100, progress)) if isinstance(progress, int) and not isinstance(progress, bool) else 0
        self._store.ota_status(self.device_id, state, progress, _clean(msg.get("error"), 80))

    # ── what a panel message does ────────────────────────────────────────────

    def _call_for(self, widget: dict, msg: dict) -> tuple[str, str, dict] | None:
        """(domain, service, data) for a message on this widget, or None if the panel may not do that."""
        entity = widget["entity"]
        domain = entity.split(".")[0] if entity else ""
        if domain not in ACTION_DOMAINS and domain not in ("select", "input_select", "number", "input_number"):
            return None
        kind = widget["type"]
        mtype = msg.get("type")
        target = {"entity_id": entity}
        state = self.hass.states.get(entity)
        if mtype == "tap" and kind == "button":
            return self._button_call(widget, domain, state)
        if mtype == "act" and kind == "media" and msg.get("action") in MEDIA_ACTIONS:
            return self._media_call(msg["action"], target, state)
        if mtype == "act" and kind == "thermostat" and msg.get("action") in THERMOSTAT_ACTIONS:
            return self._climate_call(msg["action"], target, state)
        if mtype != "set":
            return None
        if kind in ("switch", "checkbox") and isinstance(msg.get("on"), bool):
            return "homeassistant", "turn_on" if msg["on"] else "turn_off", target
        if kind in ("slider", "arc", "media") and (value := _number(msg.get("value"))) is not None:
            return self._value_call(widget, domain, value, target)
        if kind in ("dropdown", "roller") and isinstance(msg.get("option"), str):
            real = self._options.get(widget["id"], {}).get(msg["option"])
            if real is None:
                return None
            if domain == "media_player":
                return domain, "select_source", {**target, "source": real}
            return domain, "select_option", {**target, "option": real}
        return None

    def _button_call(self, widget: dict, domain: str, state: Any) -> tuple[str, str, dict] | None:
        target = {"entity_id": widget["entity"]}
        if domain in ("button", "input_button"):
            return domain, "press", target
        if domain in ("scene", "script"):
            return domain, "turn_on", target
        action = widget["action"] or DEFAULT_ACTION.get(domain, "")
        if action not in BUTTON_ACTIONS.get(domain, ()):
            return None if domain in BUTTON_ACTIONS else ("homeassistant", "toggle", target)
        if domain == "media_player":
            if action == "mute":
                muted = bool(state.attributes.get("is_volume_muted")) if state else False
                return domain, "volume_mute", {**target, "is_volume_muted": not muted}
            service = {"play_pause": "media_play_pause", "play": "media_play", "pause": "media_pause", "stop": "media_stop",
                       "next": "media_next_track", "prev": "media_previous_track", "volume_up": "volume_up", "volume_down": "volume_down"}[action]
            return domain, service, target
        if domain == "cover":
            return domain, {"toggle": "toggle", "open": "open_cover", "close": "close_cover", "stop": "stop_cover"}[action], target
        if domain == "vacuum":
            return domain, {"start": "start", "pause": "pause", "return_home": "return_to_base"}[action], target
        if domain == "climate":
            return self._climate_call(action, target, state)
        return None

    @staticmethod
    def _media_call(action: str, target: dict, state: Any) -> tuple[str, str, dict]:
        service = {"prev": "media_previous_track", "play_pause": "media_play_pause", "next": "media_next_track"}[action]
        return "media_player", service, target

    def _climate_call(self, action: str, target: dict, state: Any) -> tuple[str, str, dict] | None:
        if state is None:
            return None
        attrs = state.attributes
        current = _number(attrs.get("temperature"))
        if current is None:
            current = _number(attrs.get("current_temperature"))
        if current is None:
            return None
        step = _number(attrs.get("target_temp_step")) or (0.5 if self.hass.config.units.temperature_unit == "°C" else 1.0)
        value = current + step if action == "temp_up" else current - step
        low, high = _number(attrs.get("min_temp")), _number(attrs.get("max_temp"))
        if low is not None:
            value = max(low, value)
        if high is not None:
            value = min(high, value)
        return "climate", "set_temperature", {**target, "temperature": round(value, 1)}

    @staticmethod
    def _value_call(widget: dict, domain: str, value: float, target: dict) -> tuple[str, str, dict] | None:
        if domain in PERCENT_DOMAINS:
            pct = int(max(0, min(100, round(value))))
            if domain == "light":
                return ("light", "turn_off", target) if pct == 0 else ("light", "turn_on", {**target, "brightness_pct": pct})
            if domain == "cover":
                return "cover", "set_cover_position", {**target, "position": pct}
            if domain == "fan":
                return "fan", "set_percentage", {**target, "percentage": pct}
            return "media_player", "volume_set", {**target, "volume_level": pct / 100}
        if domain in ("input_number", "number"):
            return domain, "set_value", {**target, "value": max(widget["min"], min(widget["max"], value))}
        return None

    async def handle(self, msg: dict) -> None:
        if msg.get("type") == "ota_status":
            self._ota_status(msg)
            return
        if msg.get("type") == "page":
            await self._switch_page(msg.get("dir"))
            return
        widget = next((w for w in self._widgets if w["id"] == msg.get("widget")), None)
        if widget is None:
            return
        call = self._call_for(widget, msg)
        now = time.monotonic()
        gap = VALUE_GAP if "value" in msg else MIN_ACTION_GAP
        if call is None or now - self._last_action < gap:
            return
        self._last_action = now
        await self.hass.services.async_call(call[0], call[1], call[2], blocking=False)

    def stop_tracking(self) -> None:
        if self._unsub_states:
            self._unsub_states()
            self._unsub_states = None
        if self._unsub_time:
            self._unsub_time()
            self._unsub_time = None


class DisplaySocketView(HomeAssistantView):
    url = "/api/display/ws"
    name = "api:display:ws"
    requires_auth = False  # the device key is the credential, not a Home Assistant token

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def get(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse(heartbeat=30, max_msg_size=4096)
        await ws.prepare(request)

        session = await self._authenticate(request, ws)
        if session is None:
            return ws

        sessions: set[PanelSession] = self.hass.data[DOMAIN]["sessions"]
        store = self.hass.data[DOMAIN]["store"]
        token = object()
        sessions.add(session)
        remove_listener = store.add_listener(session.device_id, session.layout_changed)
        store.mark_online(session.device_id, token)
        store.set_info(session.device_id, session.fw, session.variant)
        try:
            session.start()
            await session.send_layout()
            async for raw in ws:
                if raw.type is not WSMsgType.TEXT:
                    continue
                try:
                    msg = raw.json()
                except ValueError:
                    continue
                if isinstance(msg, dict):
                    await session.handle(msg)
        except ConnectionResetError:
            pass
        finally:
            remove_listener()
            session.stop_tracking()
            sessions.discard(session)
            store.mark_offline(session.device_id, token)
        return ws

    async def _authenticate(self, request: web.Request, ws: web.WebSocketResponse) -> PanelSession | None:
        try:
            hello = await ws.receive_json(timeout=HELLO_TIMEOUT)
        except (TimeoutError, TypeError, ValueError):
            await ws.close(code=CLOSE_AUTH)
            return None
        key = hello.get("key") if isinstance(hello, dict) and hello.get("type") == "hello" else None
        entry = get_entry(self.hass)
        sub = find_by_key(entry, key) if entry and isinstance(key, str) and key else None
        if sub is None:
            await process_wrong_login(request)
            await ws.send_json({"type": "error", "code": "auth"})
            await ws.close(code=CLOSE_AUTH)
            return None
        return PanelSession(self.hass, ws, sub.subentry_id, key, _clean(hello.get("fw"), 32), _clean(hello.get("variant"), 16))


class DisplayPingView(HomeAssistantView):
    """Lets a panel tell 'Home Assistant is there but the integration is missing' from 'unreachable'."""

    url = "/api/display/ping"
    name = "api:display:ping"
    requires_auth = False

    async def get(self, request: web.Request) -> web.Response:
        return self.json({"integration": DOMAIN})
