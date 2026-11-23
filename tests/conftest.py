from datetime import timedelta

import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from homeassistant.auth.models import TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN
from homeassistant.config_entries import ConfigSubentryData

from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, CONF_HA_URL, DOMAIN, SUBENTRY_DEVICE


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    yield


async def make_token(hass, *, long_lived: bool = True, name: str = "panel", admin: bool = False) -> str:
    user = await hass.auth.async_create_user(name, group_ids=["system-admin" if admin else "system-users"])
    kwargs = (
        {"token_type": TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN, "access_token_expiration": timedelta(days=3650)}
        if long_lived
        else {"client_id": "https://ha.test/"}
    )
    refresh = await hass.auth.async_create_refresh_token(user, client_name=name, **kwargs)
    return hass.auth.async_create_access_token(refresh)


def device(name: str, brightness: int = 80, key: str | None = None) -> ConfigSubentryData:
    return ConfigSubentryData(
        data={CONF_BRIGHTNESS: brightness, CONF_DEVICE_KEY: key or f"key-{name.lower()}"},
        subentry_type=SUBENTRY_DEVICE,
        title=name,
        unique_id=name.lower(),
    )


@pytest.fixture
def make_entry(hass):
    def make(*devices: ConfigSubentryData) -> MockConfigEntry:
        entry = MockConfigEntry(
            domain=DOMAIN,
            title="ESP32 Display",
            data={CONF_HA_URL: "http://ha.test:8123"},
            subentries_data=list(devices),
        )
        entry.add_to_hass(hass)
        return entry

    return make
