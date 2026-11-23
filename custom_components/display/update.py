"""Firmware update entity per panel: shows what the panel runs, installs the bundled image over the air."""
from __future__ import annotations

import asyncio
from typing import Any

from homeassistant.components.update import UpdateDeviceClass, UpdateEntity, UpdateEntityFeature
from homeassistant.config_entries import ConfigEntry, ConfigSubentry
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN
from .devices import device_subentries
from .firmware import bundle_of
from .layouts import LayoutStore, info_signal, online_signal

OTA_TIMEOUT = 300  # seconds from "go" until the panel is back on the new version


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    for sub in device_subentries(entry):
        async_add_entities([PanelFirmware(hass, sub)], config_subentry_id=sub.subentry_id)


class PanelFirmware(UpdateEntity):
    _attr_has_entity_name = True
    _attr_should_poll = False
    _attr_translation_key = "firmware"
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_supported_features = UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS
    _attr_title = "ESP32 Display"
    _attr_release_url = "https://github.com/hypersonic30/ha-display/releases"

    def __init__(self, hass: HomeAssistant, sub: ConfigSubentry) -> None:
        self._hass = hass
        self._store: LayoutStore = hass.data[DOMAIN]["store"]
        self._device_id = sub.subentry_id
        self._attr_unique_id = f"{sub.subentry_id}_firmware"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, sub.subentry_id)}, name=sub.title)

    @property
    def installed_version(self) -> str | None:
        return self._store.info(self._device_id)["fw"]

    @property
    def latest_version(self) -> str | None:
        bundle = bundle_of(self._hass)
        installed = self.installed_version
        variant = self._store.info(self._device_id)["variant"]
        if bundle is None or (variant and variant not in bundle["variants"]):
            return installed  # nothing to offer for this panel
        return bundle["version"]

    @property
    def in_progress(self) -> bool:
        run = self._store.ota(self._device_id)
        return bool(run and not run.future.done())

    @property
    def update_percentage(self) -> int | None:
        run = self._store.ota(self._device_id)
        return run.progress if run and not run.future.done() else None

    async def async_added_to_hass(self) -> None:
        @callback
        def changed() -> None:
            self.async_write_ha_state()

        for signal in (info_signal(self._device_id), online_signal(self._device_id)):
            self.async_on_remove(async_dispatcher_connect(self.hass, signal, changed))

    async def async_install(self, version: str | None, backup: bool, **kwargs: Any) -> None:
        session = next((s for s in self._hass.data[DOMAIN]["sessions"] if s.device_id == self._device_id), None)
        if session is None:
            raise HomeAssistantError("The display is not connected - switch it on and try again")
        bundle = bundle_of(self._hass)
        info = bundle["variants"].get(session.variant) if bundle else None
        if info is None:
            raise HomeAssistantError(f"This integration has no firmware for the display variant '{session.variant or 'unknown'}'")
        future = self._store.ota_begin(self._device_id, bundle["version"])
        try:
            await session.start_ota(bundle["version"], info)
            await asyncio.wait_for(future, OTA_TIMEOUT)
        except TimeoutError as err:
            raise HomeAssistantError("The display did not report back after the update - check it, it may have fallen back") from err
        finally:
            self._store.ota_end(self._device_id)
