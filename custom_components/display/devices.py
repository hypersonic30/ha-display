"""Helpers for the panels (device subentries) of the one config entry."""
from __future__ import annotations

from homeassistant.config_entries import ConfigEntry, ConfigSubentry
from homeassistant.core import HomeAssistant
from homeassistant.util import slugify

from .const import CONF_BRIGHTNESS, DEFAULT_BRIGHTNESS, DOMAIN, SUBENTRY_DEVICE


def get_entry(hass: HomeAssistant) -> ConfigEntry | None:
    entries = hass.config_entries.async_entries(DOMAIN)
    return entries[0] if entries else None


def device_subentries(entry: ConfigEntry) -> list[ConfigSubentry]:
    return [s for s in entry.subentries.values() if s.subentry_type == SUBENTRY_DEVICE]


def name_key(name: str) -> str:
    """Case-, space- and umlaut-insensitive key, so 'Küche' on the panel matches 'kuche ' in HA."""
    return slugify(name.strip())


def find_device(entry: ConfigEntry, typed_name: str) -> ConfigSubentry | None:
    key = name_key(typed_name)
    if not key:
        return None
    return next((s for s in device_subentries(entry) if name_key(s.title) == key), None)


def brightness_of(sub: ConfigSubentry) -> int:
    return int(sub.data.get(CONF_BRIGHTNESS, DEFAULT_BRIGHTNESS))
