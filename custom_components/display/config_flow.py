from __future__ import annotations

from collections.abc import Mapping
from typing import Any
from urllib.parse import urlparse

import voluptuous as vol

from homeassistant.auth.models import TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN
from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    ConfigSubentryFlow,
    SubentryFlowResult,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.network import NoURLAvailableError, get_url
from homeassistant.helpers.selector import (
    NumberSelector,
    NumberSelectorConfig,
    NumberSelectorMode,
    TextSelector,
    TextSelectorConfig,
    TextSelectorType,
)

from .const import CONF_BRIGHTNESS, CONF_HA_URL, CONF_NAME, CONF_TOKEN, DEFAULT_BRIGHTNESS, DOMAIN, SUBENTRY_DEVICE
from .devices import device_subentries, name_key

_PASSWORD = TextSelector(TextSelectorConfig(type=TextSelectorType.PASSWORD))


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


def _check_token(hass: HomeAssistant, token: str) -> str | None:
    """Error key, or None. Checked locally: no request to Home Assistant itself is needed."""
    refresh = hass.auth.async_validate_access_token(token.strip())
    if refresh is None:
        return "invalid_auth"
    if refresh.token_type != TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN:
        return "not_long_lived"
    return None


def _suggested_url(hass: HomeAssistant) -> str:
    try:
        return get_url(hass, prefer_external=False, allow_cloud=False)
    except NoURLAvailableError:
        return ""


def _connection_schema(url: str, with_url: bool = True) -> vol.Schema:
    schema: dict = {}
    if with_url:
        schema[vol.Required(CONF_HA_URL, default=url)] = str
    schema[vol.Required(CONF_TOKEN)] = _PASSWORD
    return vol.Schema(schema)


class DisplayConfigFlow(ConfigFlow, domain=DOMAIN):
    """One connection for all panels: the Home Assistant address and one long-lived token."""

    VERSION = 1

    @classmethod
    @callback
    def async_get_supported_subentry_types(cls, config_entry: ConfigEntry) -> dict[str, type[ConfigSubentryFlow]]:
        return {SUBENTRY_DEVICE: DeviceSubentryFlow}

    async def _validated(self, user_input: dict[str, Any]) -> tuple[dict[str, Any], dict[str, str]]:
        errors: dict[str, str] = {}
        data = {CONF_TOKEN: user_input[CONF_TOKEN].strip()}
        if CONF_HA_URL in user_input:
            url = _clean_url(user_input[CONF_HA_URL])
            if url is None:
                errors[CONF_HA_URL] = "invalid_url"
            data[CONF_HA_URL] = url or user_input[CONF_HA_URL]
        if (error := _check_token(self.hass, data[CONF_TOKEN])) is not None:
            errors[CONF_TOKEN] = error
        return data, errors

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            data, errors = await self._validated(user_input)
            if not errors:
                return self.async_create_entry(title="ESP32 Display", data=data)
        shown = user_input or {}
        return self.async_show_form(
            step_id="user",
            data_schema=_connection_schema(shown.get(CONF_HA_URL) or _suggested_url(self.hass)),
            errors=errors,
        )

    async def async_step_reconfigure(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reconfigure_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            data, errors = await self._validated(user_input)
            if not errors:
                return self.async_update_reload_and_abort(entry, data_updates=data)
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=_connection_schema((user_input or {}).get(CONF_HA_URL) or entry.data[CONF_HA_URL]),
            errors=errors,
        )

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            data, errors = await self._validated(user_input)
            if not errors:
                return self.async_update_reload_and_abort(self._get_reauth_entry(), data_updates=data)
        return self.async_show_form(step_id="reauth_confirm", data_schema=_connection_schema("", with_url=False), errors=errors)


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
                data = {CONF_BRIGHTNESS: int(user_input[CONF_BRIGHTNESS])}
                if sub is not None:
                    return self.async_update_and_abort(entry, sub, title=name, data=data)
                return self.async_create_entry(title=name, data=data, unique_id=key)
        shown = {CONF_NAME: sub.title if sub else "", CONF_BRIGHTNESS: sub.data.get(CONF_BRIGHTNESS, DEFAULT_BRIGHTNESS) if sub else DEFAULT_BRIGHTNESS}
        shown.update(user_input or {})
        schema = vol.Schema(
            {
                vol.Required(CONF_NAME, default=shown[CONF_NAME]): str,
                vol.Required(CONF_BRIGHTNESS, default=shown[CONF_BRIGHTNESS]): NumberSelector(
                    NumberSelectorConfig(min=5, max=100, step=5, unit_of_measurement="%", mode=NumberSelectorMode.SLIDER)
                ),
            }
        )
        return self.async_show_form(step_id=step, data_schema=schema, errors=errors)
