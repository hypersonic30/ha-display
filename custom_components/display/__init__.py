from __future__ import annotations

from pathlib import Path

from homeassistant.components.frontend import add_extra_js_url
from homeassistant.components.http import StaticPathConfig
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import Event, HomeAssistant
from homeassistant.helpers import config_validation as cv
from homeassistant.loader import async_get_integration
from homeassistant.helpers.typing import ConfigType

from . import websocket_api
from .const import CARD_FILE, CONF_DEVICE_KEY, DOMAIN, STATIC_PATH
from .devices import device_subentries
from .layouts import LayoutStore
from .panel import CLOSE_GONE, DisplayPingView, DisplaySocketView

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)
PLATFORMS = ["binary_sensor"]


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Runs once per start: the card, the layout store and the WebSocket commands."""
    integration = await async_get_integration(hass, DOMAIN)
    www = str(Path(__file__).parent / "www")
    await hass.http.async_register_static_paths([StaticPathConfig(STATIC_PATH, www, cache_headers=False)])
    # The version in the URL makes browsers pick up a new card after an update.
    add_extra_js_url(hass, f"{STATIC_PATH}/{CARD_FILE}?v={integration.version}")

    store = LayoutStore(hass)
    await store.async_load()
    hass.data[DOMAIN] = {"store": store, "sessions": set()}
    websocket_api.async_register(hass)
    hass.http.register_view(DisplaySocketView(hass))
    hass.http.register_view(DisplayPingView())

    async def close_panels(_: Event) -> None:
        for session in list(hass.data[DOMAIN]["sessions"]):
            await session.close(1001, "shutdown")

    hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, close_panels)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    store: LayoutStore = hass.data[DOMAIN]["store"]
    devices = device_subentries(entry)
    await store.async_prune({sub.subentry_id for sub in devices})
    # A removed display, or one whose device key was renewed, must not stay connected.
    keys = {sub.subentry_id: sub.data[CONF_DEVICE_KEY] for sub in devices}
    for session in list(hass.data[DOMAIN]["sessions"]):
        if keys.get(session.device_id) != session.key:
            await session.close(CLOSE_GONE, "gone")

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_reload))
    # Panels that stay connected across a reload get changed settings (e.g. brightness) right away.
    for sub in devices:
        store.notify(sub.subentry_id)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: ConfigEntry) -> None:
    for session in list(hass.data[DOMAIN]["sessions"]):
        await session.close(CLOSE_GONE, "gone")
    await hass.data[DOMAIN]["store"].async_clear()


async def _reload(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
