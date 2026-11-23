"""WebSocket commands: the Lovelace card edits layouts, the panels subscribe to them."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback

from .const import CONF_HA_URL, CONF_TOKEN, DOMAIN
from .devices import brightness_of, device_subentries, find_device, get_entry
from .layouts import LayoutStore, validate_widgets


def _store(hass: HomeAssistant) -> LayoutStore:
    return hass.data[DOMAIN]["store"]


def _payload(hass: HomeAssistant, device_id: str) -> dict | None:
    entry = get_entry(hass)
    sub = entry.subentries.get(device_id) if entry else None
    if sub is None:
        return None
    layout = _store(hass).get(device_id)
    return {
        "name": sub.title,
        "rev": layout["rev"],
        "widgets": layout["widgets"],
        "settings": {"brightness": brightness_of(sub)},
    }


def _resolve(hass: HomeAssistant, connection, msg):
    """Subentry of the requested device_id, or None after answering with an error."""
    entry = get_entry(hass)
    sub = entry.subentries.get(msg["device_id"]) if entry else None
    if sub is None:
        connection.send_error(msg["id"], websocket_api.ERR_NOT_FOUND, "Unknown display device")
    return sub


@callback
def async_register(hass: HomeAssistant) -> None:
    for command in (ws_devices, ws_layout_get, ws_layout_set, ws_setup_info, ws_layout_subscribe):
        websocket_api.async_register_command(hass, command)


@websocket_api.websocket_command({vol.Required("type"): "display/devices"})
@callback
def ws_devices(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    entry = get_entry(hass)
    store = _store(hass)
    devices = [
        {"id": sub.subentry_id, "name": sub.title, "online": store.is_online(sub.subentry_id)}
        for sub in (device_subentries(entry) if entry else [])
    ]
    connection.send_result(msg["id"], {"devices": sorted(devices, key=lambda d: d["name"].lower())})


@websocket_api.websocket_command({vol.Required("type"): "display/layout/get", vol.Required("device_id"): str})
@callback
def ws_layout_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    if _resolve(hass, connection, msg) is None:
        return
    connection.send_result(msg["id"], _payload(hass, msg["device_id"]))


@websocket_api.websocket_command(
    {vol.Required("type"): "display/layout/set", vol.Required("device_id"): str, vol.Required("widgets"): list}
)
@websocket_api.require_admin
@websocket_api.async_response
async def ws_layout_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    if _resolve(hass, connection, msg) is None:
        return
    try:
        widgets = validate_widgets(msg["widgets"])
    except vol.Invalid as err:
        connection.send_error(msg["id"], websocket_api.ERR_INVALID_FORMAT, str(err))
        return
    rev = await _store(hass).async_set(msg["device_id"], widgets)
    connection.send_result(msg["id"], {"rev": rev, "widgets": widgets})


@websocket_api.websocket_command({vol.Required("type"): "display/setup_info", vol.Required("device_id"): str})
@websocket_api.require_admin
@callback
def ws_setup_info(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """What to type into the panel's setup page (the one token of this integration)."""
    sub = _resolve(hass, connection, msg)
    if sub is None:
        return
    entry = get_entry(hass)
    connection.send_result(msg["id"], {"name": sub.title, CONF_HA_URL: entry.data[CONF_HA_URL], CONF_TOKEN: entry.data[CONF_TOKEN]})


@websocket_api.websocket_command({vol.Required("type"): "display/layout/subscribe", vol.Required("device"): str})
@callback
def ws_layout_subscribe(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Used by the ESP32: the name typed on the panel picks the device; layout changes are pushed."""
    entry = get_entry(hass)
    sub = find_device(entry, msg["device"]) if entry else None
    if sub is None:
        connection.send_error(msg["id"], websocket_api.ERR_NOT_FOUND, f"Display device '{msg['device']}' is not set up in Home Assistant")
        return

    device_id = sub.subentry_id
    store = _store(hass)
    token = object()

    @callback
    def push() -> None:
        payload = _payload(hass, device_id)
        if payload is not None:
            connection.send_message(websocket_api.event_message(msg["id"], payload))

    remove_listener = store.add_listener(device_id, push)

    @callback
    def unsubscribe() -> None:
        remove_listener()
        store.mark_offline(device_id, token)

    connection.subscriptions[msg["id"]] = unsubscribe
    connection.send_result(msg["id"])
    store.mark_online(device_id, token)
    push()
