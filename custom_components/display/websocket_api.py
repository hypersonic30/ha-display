"""WebSocket commands: the Lovelace card edits layouts, the panels subscribe to them."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback

from .const import CONF_DEVICE_KEY, CONF_HA_URL, DOMAIN
from .devices import brightness_of, device_subentries, get_entry, rotation_of, screen_size
from .layouts import LayoutStore, validate_layout


def _store(hass: HomeAssistant) -> LayoutStore:
    return hass.data[DOMAIN]["store"]


def _payload(hass: HomeAssistant, device_id: str) -> dict | None:
    """Everything the card needs to edit one display."""
    entry = get_entry(hass)
    sub = entry.subentries.get(device_id) if entry else None
    if sub is None:
        return None
    layout = _store(hass).get(device_id)
    width, height = screen_size(rotation_of(sub))
    return {
        "name": sub.title,
        "rev": layout["rev"],
        "pages": layout["pages"],
        "pager": layout["pager"],
        "screen": {"w": width, "h": height},
        "settings": {"brightness": brightness_of(sub), "rotation": rotation_of(sub)},
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
    for command in (ws_devices, ws_layout_get, ws_layout_set, ws_setup_info):
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
    {
        vol.Required("type"): "display/layout/set",
        vol.Required("device_id"): str,
        vol.Required("pages"): list,
        vol.Optional("pager"): vol.Any(None, dict),
    }
)
@websocket_api.require_admin
@websocket_api.async_response
async def ws_layout_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    sub = _resolve(hass, connection, msg)
    if sub is None:
        return
    screen = screen_size(rotation_of(sub))
    try:
        pages, pager = validate_layout(msg["pages"], msg.get("pager"), screen)
    except vol.Invalid as err:
        connection.send_error(msg["id"], websocket_api.ERR_INVALID_FORMAT, str(err))
        return
    await _store(hass).async_set(msg["device_id"], pages, pager, screen)
    connection.send_result(msg["id"], _payload(hass, msg["device_id"]))


@websocket_api.websocket_command({vol.Required("type"): "display/setup_info", vol.Required("device_id"): str})
@websocket_api.require_admin
@callback
def ws_setup_info(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """What the panel's setup page needs: the address and this panel's own device key."""
    sub = _resolve(hass, connection, msg)
    if sub is None:
        return
    entry = get_entry(hass)
    connection.send_result(msg["id"], {"name": sub.title, CONF_HA_URL: entry.data[CONF_HA_URL], CONF_DEVICE_KEY: sub.data[CONF_DEVICE_KEY]})
