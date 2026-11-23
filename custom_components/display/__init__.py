from __future__ import annotations

from pathlib import Path

from homeassistant.components.frontend import add_extra_js_url
from homeassistant.components.http import StaticPathConfig
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers import config_validation as cv
from homeassistant.loader import async_get_integration
from homeassistant.helpers.typing import ConfigType

from . import websocket_api
from .const import CARD_FILE, CONF_TOKEN, DOMAIN, STATIC_PATH
from .devices import device_subentries
from .layouts import LayoutStore

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
    hass.data[DOMAIN] = {"store": store}
    websocket_api.async_register(hass)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    # The panels log in with this token, so a revoked one means every panel is locked out.
    if hass.auth.async_validate_access_token(entry.data[CONF_TOKEN]) is None:
        raise ConfigEntryAuthFailed("The access token of the display integration is no longer valid")

    store: LayoutStore = hass.data[DOMAIN]["store"]
    devices = device_subentries(entry)
    await store.async_prune({sub.subentry_id for sub in devices})

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_reload))
    # Panels that stay connected across a reload get changed settings (e.g. brightness) right away.
    for sub in devices:
        store.notify(sub.subentry_id)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.data[DOMAIN]["store"].async_clear()


async def _reload(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
