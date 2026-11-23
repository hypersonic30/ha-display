from __future__ import annotations

from homeassistant.core import HomeAssistant
from homeassistant.config_entries import ConfigEntry
from homeassistant.components.http import StaticPathConfig
from homeassistant.components.frontend import add_extra_js_url

from .const import DOMAIN, WS_PATH, CARD_FILE
from .views import LayoutStore, DisplayLayoutView, DisplayDevicesView

_STORE_KEY = f"{DOMAIN}_store"


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    hass.data.setdefault(DOMAIN, {})

    # Serve the Lovelace card JS
    www_path = hass.config.path(f"custom_components/{DOMAIN}/www")
    await hass.http.async_register_static_paths(
        [StaticPathConfig(WS_PATH, www_path, cache_headers=False)]
    )
    # Auto-register card — no manual Lovelace resource entry needed after HACS install
    add_extra_js_url(hass, f"{WS_PATH}/{CARD_FILE}")

    # Shared layout store (one instance for all config entries)
    store = LayoutStore(hass)
    await store.async_load()
    hass.data[DOMAIN][_STORE_KEY] = store

    # Register REST views
    hass.http.register_view(DisplayLayoutView(store))
    hass.http.register_view(DisplayDevicesView(store))

    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hass.data[DOMAIN][entry.entry_id] = entry.data
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hass.data[DOMAIN].pop(entry.entry_id, None)
    return True
