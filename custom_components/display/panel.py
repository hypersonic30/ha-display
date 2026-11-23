"""The panels' own endpoint: display -> integration -> Home Assistant.

A panel never holds a Home Assistant token. It proves who it is with its device key, receives its layout and the
states of the entities in it, and reports taps. What a tap does is decided here, from the stored layout, so a panel
can only operate the widgets it was given.
"""
from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from aiohttp import WSMsgType, web

from homeassistant.components.http import HomeAssistantView
from homeassistant.components.http.ban import process_wrong_login
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

from .const import ACTION_DOMAINS, DOMAIN
from .devices import brightness_of, find_by_key, get_entry, rotation_of, screen_size

_LOGGER = logging.getLogger(__name__)

HELLO_TIMEOUT = 10
MAX_STATE_BYTES = 47          # the firmware keeps a state in a 48 byte buffer
MIN_ACTION_GAP = 0.1          # seconds between taps of one panel
CLOSE_AUTH = 4001
CLOSE_GONE = 4002


def _short(state: str) -> str:
    return state.encode()[:MAX_STATE_BYTES].decode(errors="ignore")


def _service_for(widget: dict, msg: dict) -> tuple[str, str] | None:
    """(domain, service) for a tap/set on this widget, or None if the panel may not do that."""
    entity = widget["entity"]
    domain = entity.split(".")[0] if entity else ""
    if domain not in ACTION_DOMAINS:
        return None
    if widget["type"] == "button" and msg.get("type") == "tap":
        if domain in ("button", "input_button"):
            return domain, "press"
        if domain in ("scene", "script"):
            return domain, "turn_on"
        return "homeassistant", "toggle"
    if widget["type"] == "switch" and msg.get("type") == "set" and isinstance(msg.get("on"), bool):
        return "homeassistant", "turn_on" if msg["on"] else "turn_off"
    return None


class PanelSession:
    """One connected, authenticated panel."""

    def __init__(self, hass: HomeAssistant, ws: web.WebSocketResponse, device_id: str, key: str) -> None:
        self.hass = hass
        self.ws = ws
        self.device_id = device_id
        self.key = key
        self._store = hass.data[DOMAIN]["store"]
        self._lock = asyncio.Lock()
        self._widgets: list[dict] = []
        self._page = 0
        self._unsub_states = None
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
        return {
            "type": "layout",
            "name": sub.title,
            "rev": layout["rev"],
            "settings": {"brightness": brightness_of(sub), "rotation": rotation_of(sub)},
            "screen": {"w": width, "h": height},
            "page": {"index": self._page, "count": len(pages), "name": page["name"]},
            # Only worth showing when there is somewhere to go.
            "pager": layout["pager"] if len(pages) > 1 else None,
            "widgets": page["widgets"],
        }

    def _state_of(self, entity_id: str) -> str:
        state = self.hass.states.get(entity_id)
        return _short(state.state) if state else "unavailable"

    async def send_layout(self) -> None:
        payload = self._layout_payload()
        if payload is None:
            await self.close(CLOSE_GONE, "gone")
            return
        self._widgets = payload["widgets"]
        entities = sorted({w["entity"] for w in self._widgets if w["entity"]})
        if self._unsub_states:
            self._unsub_states()
            self._unsub_states = None
        if entities:
            self._unsub_states = async_track_state_change_event(self.hass, entities, self._on_state)
        await self._send(payload)
        await self._send({"type": "states", "states": {e: self._state_of(e) for e in entities}})

    @callback
    def layout_changed(self) -> None:
        self.hass.async_create_task(self.send_layout())

    @callback
    def _on_state(self, event: Event[EventStateChangedData]) -> None:
        new = event.data["new_state"]
        self.hass.async_create_task(
            self._send({"type": "states", "states": {event.data["entity_id"]: _short(new.state) if new else "unavailable"}})
        )

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

    async def handle(self, msg: dict) -> None:
        if msg.get("type") == "page":
            await self._switch_page(msg.get("dir"))
            return
        widget = next((w for w in self._widgets if w["id"] == msg.get("widget")), None)
        if widget is None:
            return
        target = _service_for(widget, msg)
        now = time.monotonic()
        if target is None or now - self._last_action < MIN_ACTION_GAP:
            return
        self._last_action = now
        await self.hass.services.async_call(target[0], target[1], {"entity_id": widget["entity"]}, blocking=False)

    def stop_tracking(self) -> None:
        if self._unsub_states:
            self._unsub_states()
            self._unsub_states = None


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
        try:
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
        return PanelSession(self.hass, ws, sub.subentry_id, key)


class DisplayPingView(HomeAssistantView):
    """Lets a panel tell 'Home Assistant is there but the integration is missing' from 'unreachable'."""

    url = "/api/display/ping"
    name = "api:display:ping"
    requires_auth = False

    async def get(self, request: web.Request) -> web.Response:
        return self.json({"integration": DOMAIN})
