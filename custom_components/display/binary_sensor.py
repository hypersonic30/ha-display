"""'Connected' sensor per panel: on while the ESP32 holds its layout subscription."""
from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.config_entries import ConfigEntry, ConfigSubentry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN
from .devices import device_subentries
from .layouts import LayoutStore, online_signal


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    store: LayoutStore = hass.data[DOMAIN]["store"]
    for sub in device_subentries(entry):
        async_add_entities([PanelConnected(store, sub)], config_subentry_id=sub.subentry_id)


class PanelConnected(BinarySensorEntity):
    _attr_has_entity_name = True
    _attr_should_poll = False
    _attr_translation_key = "connected"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(self, store: LayoutStore, sub: ConfigSubentry) -> None:
        self._store = store
        self._device_id = sub.subentry_id
        self._attr_unique_id = f"{sub.subentry_id}_connected"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, sub.subentry_id)},
            name=sub.title,
            manufacturer="DIY",
            model="ESP32 + ILI9488 480x320",
        )

    @property
    def is_on(self) -> bool:
        return self._store.is_online(self._device_id)

    async def async_added_to_hass(self) -> None:
        @callback
        def changed() -> None:
            self.async_write_ha_state()

        self.async_on_remove(async_dispatcher_connect(self.hass, online_signal(self._device_id), changed))
