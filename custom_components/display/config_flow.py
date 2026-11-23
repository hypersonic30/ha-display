from __future__ import annotations

from typing import Any
from urllib.parse import urlparse

import voluptuous as vol

from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    ConfigSubentryFlow,
    SubentryFlowResult,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.network import NoURLAvailableError, get_url
from homeassistant.helpers.selector import NumberSelector, NumberSelectorConfig, NumberSelectorMode

from .const import (
    CONF_BRIGHTNESS,
    CONF_DEVICE_KEY,
    CONF_HA_URL,
    CONF_NAME,
    CONF_NEW_KEY,
    DEFAULT_BRIGHTNESS,
    DOMAIN,
    SUBENTRY_DEVICE,
)
from .devices import device_subentries, name_key, new_device_key


def _clean_url(raw: str) -> str | None:
    """'http://host:8123/' -> 'http://host:8123'; None if it is not an http(s) address."""
    parsed = urlparse(raw.strip())
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        return None
    try:
        port = parsed.port
    except ValueError:
        return None
    host = f"[{parsed.hostname}]" if ":" in parsed.hostname else parsed.hostname
    return f"{parsed.scheme}://{host}" + (f":{port}" if port else "")


def _suggested_url(hass: HomeAssistant) -> str:
    try:
        return get_url(hass, prefer_external=False, allow_cloud=False)
    except NoURLAvailableError:
        return ""


class DisplayConfigFlow(ConfigFlow, domain=DOMAIN):
    """One connection for all panels: how the panels reach Home Assistant. No token is involved."""

    VERSION = 1

    @classmethod
    @callback
    def async_get_supported_subentry_types(cls, config_entry: ConfigEntry) -> dict[str, type[ConfigSubentryFlow]]:
        return {SUBENTRY_DEVICE: DeviceSubentryFlow}

    async def _form(self, step: str, user_input: dict[str, Any] | None, current: str) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            url = _clean_url(user_input[CONF_HA_URL])
            if url is None:
                errors[CONF_HA_URL] = "invalid_url"
            else:
                if step == "reconfigure":
                    return self.async_update_reload_and_abort(self._get_reconfigure_entry(), data_updates={CONF_HA_URL: url})
                return self.async_create_entry(title="ESP32 Display", data={CONF_HA_URL: url})
        shown = (user_input or {}).get(CONF_HA_URL) or current
        return self.async_show_form(step_id=step, data_schema=vol.Schema({vol.Required(CONF_HA_URL, default=shown): str}), errors=errors)

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        return await self._form("user", user_input, _suggested_url(self.hass))

    async def async_step_reconfigure(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        return await self._form("reconfigure", user_input, self._get_reconfigure_entry().data[CONF_HA_URL])


class DeviceSubentryFlow(ConfigSubentryFlow):
    """One panel. Its name must be typed the same way on the panel's setup page."""

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> SubentryFlowResult:
        return await self._form("user", user_input)

    async def async_step_reconfigure(self, user_input: dict[str, Any] | None = None) -> SubentryFlowResult:
        return await self._form("reconfigure", user_input)

    async def _form(self, step: str, user_input: dict[str, Any] | None) -> SubentryFlowResult:
        entry = self._get_entry()
        sub = self._get_reconfigure_subentry() if step == "reconfigure" else None
        errors: dict[str, str] = {}
        if user_input is not None:
            name = user_input[CONF_NAME].strip()
            key = name_key(name)
            others = {name_key(s.title) for s in device_subentries(entry) if s is not sub}
            if not key:
                errors[CONF_NAME] = "name_invalid"
            elif len(name.encode()) > 31:
                errors[CONF_NAME] = "name_too_long"
            elif key in others:
                errors[CONF_NAME] = "name_taken"
            if not errors:
                data = {CONF_BRIGHTNESS: int(user_input[CONF_BRIGHTNESS]), CONF_DEVICE_KEY: sub.data[CONF_DEVICE_KEY] if sub else new_device_key()}
                if sub is not None and user_input.get(CONF_NEW_KEY):
                    data[CONF_DEVICE_KEY] = new_device_key()
                if sub is not None:
                    return self.async_update_and_abort(entry, sub, title=name, data=data)
                return self.async_create_entry(title=name, data=data, unique_id=key)
        shown = {CONF_NAME: sub.title if sub else "", CONF_BRIGHTNESS: sub.data.get(CONF_BRIGHTNESS, DEFAULT_BRIGHTNESS) if sub else DEFAULT_BRIGHTNESS}
        shown.update(user_input or {})
        fields: dict = {
            vol.Required(CONF_NAME, default=shown[CONF_NAME]): str,
            vol.Required(CONF_BRIGHTNESS, default=shown[CONF_BRIGHTNESS]): NumberSelector(
                NumberSelectorConfig(min=5, max=100, step=5, unit_of_measurement="%", mode=NumberSelectorMode.SLIDER)
            ),
        }
        if sub is not None:
            fields[vol.Required(CONF_NEW_KEY, default=False)] = bool
        schema = vol.Schema(fields)
        return self.async_show_form(step_id=step, data_schema=schema, errors=errors)
