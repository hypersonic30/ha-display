from __future__ import annotations

import os
from homeassistant.core import HomeAssistant
from homeassistant.config_entries import ConfigEntry
from homeassistant.components.http import StaticPathConfig

from .const import DOMAIN, WS_PATH


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    www_path = hass.config.path(f"custom_components/{DOMAIN}/www")
    await hass.http.async_register_static_paths(
        [StaticPathConfig(WS_PATH, www_path, cache_headers=False)]
    )
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = entry.data
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hass.data[DOMAIN].pop(entry.entry_id, None)
    return True
