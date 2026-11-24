"""Services of the integration: `display.beep`."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er

from .beep import build_tones
from .const import BEEP_PRESETS, DOMAIN
from .devices import get_entry

SERVICE_BEEP = "beep"

BEEP_SCHEMA = vol.Schema(
    {
        vol.Optional("device_id"): vol.All(cv.ensure_list, [str]),
        vol.Optional("entity_id"): vol.All(cv.ensure_list, [str]),
        vol.Optional("preset"): vol.In(tuple(BEEP_PRESETS)),
        # No range here on purpose: the numbers are clamped in build_tones() so odd YAML cannot get through.
        vol.Optional("frequency"): vol.Coerce(int),
        vol.Optional("duration_ms"): vol.Coerce(int),
        vol.Optional("repeat"): vol.Coerce(int),
    }
)


def _targets(hass: HomeAssistant, call: ServiceCall) -> list[str]:
    """Subentry ids of the displays this call addresses (device or one of its entities)."""
    entry = get_entry(hass)
    devices = dr.async_get(hass)
    entities = er.async_get(hass)
    device_ids = list(call.data.get("device_id", []))
    for entity_id in call.data.get("entity_id", []):
        entity = entities.async_get(entity_id)
        if entity is None or entity.device_id is None:
            raise ServiceValidationError(translation_domain=DOMAIN, translation_key="not_a_display", translation_placeholders={"target": entity_id})
        device_ids.append(entity.device_id)
    if not device_ids:
        raise ServiceValidationError(translation_domain=DOMAIN, translation_key="no_target")
    found: list[str] = []
    for device_id in dict.fromkeys(device_ids):
        device = devices.async_get(device_id)
        sub_id = next((ident[1] for ident in device.identifiers if ident[0] == DOMAIN), None) if device else None
        if entry is None or sub_id is None or sub_id not in entry.subentries:
            raise ServiceValidationError(translation_domain=DOMAIN, translation_key="not_a_display", translation_placeholders={"target": device_id})
        found.append(sub_id)
    return found


async def _beep(hass: HomeAssistant, call: ServiceCall) -> None:
    entry = get_entry(hass)
    targets = _targets(hass, call)
    sessions = {s.device_id: s for s in hass.data[DOMAIN]["sessions"]}
    for sub_id in targets:   # every display has to be reachable before one of them beeps
        if sub_id not in sessions:
            raise ServiceValidationError(
                translation_domain=DOMAIN, translation_key="display_offline", translation_placeholders={"name": entry.subentries[sub_id].title}
            )
    tones = build_tones(call.data.get("preset"), call.data.get("frequency"), call.data.get("duration_ms"), call.data.get("repeat"))
    for sub_id in targets:
        await sessions[sub_id].beep(tones)


def async_setup_services(hass: HomeAssistant) -> None:
    async def handle(call: ServiceCall) -> None:
        await _beep(hass, call)

    hass.services.async_register(DOMAIN, SERVICE_BEEP, handle, schema=BEEP_SCHEMA)
