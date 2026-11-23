"""Helpers for the panels (device subentries) of the one config entry."""
from __future__ import annotations

import hmac
import secrets

from homeassistant.config_entries import ConfigEntry, ConfigSubentry
from homeassistant.core import HomeAssistant
from homeassistant.util import slugify

from .const import (
    CONF_BRIGHTNESS,
    CONF_DEVICE_KEY,
    CONF_ROTATION,
    DEFAULT_BRIGHTNESS,
    DEFAULT_ROTATION,
    DOMAIN,
    SCREEN_H,
    SCREEN_W,
    SUBENTRY_DEVICE,
)


def get_entry(hass: HomeAssistant) -> ConfigEntry | None:
    entries = hass.config_entries.async_entries(DOMAIN)
    return entries[0] if entries else None


def device_subentries(entry: ConfigEntry) -> list[ConfigSubentry]:
    return [s for s in entry.subentries.values() if s.subentry_type == SUBENTRY_DEVICE]


def name_key(name: str) -> str:
    """Case-, space- and umlaut-insensitive key, so 'Küche' on the panel matches 'kuche ' in HA."""
    return slugify(name.strip())


def new_device_key() -> str:
    return secrets.token_urlsafe(24)


def find_by_key(entry: ConfigEntry, key: str) -> ConfigSubentry | None:
    """The panel that owns this device key (constant-time comparison)."""
    found = None
    for sub in device_subentries(entry):
        if hmac.compare_digest(str(sub.data.get(CONF_DEVICE_KEY, "")).encode(), key.encode()):
            found = sub
    return found


def brightness_of(sub: ConfigSubentry) -> int:
    return int(sub.data.get(CONF_BRIGHTNESS, DEFAULT_BRIGHTNESS))


def rotation_of(sub: ConfigSubentry) -> int:
    return int(sub.data.get(CONF_ROTATION, DEFAULT_ROTATION)) % 4


def screen_size(rotation: int) -> tuple[int, int]:
    """Pixel size of the screen: 0 and 2 are landscape, 1 and 3 portrait."""
    return (SCREEN_H, SCREEN_W) if rotation % 2 else (SCREEN_W, SCREEN_H)
