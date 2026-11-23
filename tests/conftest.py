import hashlib
import json
from datetime import timedelta

import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from homeassistant.setup import async_setup_component

from homeassistant.auth.models import TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN
from homeassistant.config_entries import ConfigSubentryData

from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, CONF_HA_URL, CONF_ROTATION, DOMAIN, SUBENTRY_DEVICE


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    yield


FW_LATEST = "9.9.9"


@pytest.fixture
def fake_firmware(tmp_path, monkeypatch):
    """A tiny firmware bundle (two variants) instead of the real images."""
    from custom_components.display import firmware  # noqa: PLC0415

    files = {"ili9488": b"\x01" * 1500, "ili9481": b"\x02" * 1600}
    variants = {}
    for name, data in files.items():
        (tmp_path / f"firmware-{name}.bin").write_bytes(data)
        variants[name] = {"file": f"firmware-{name}.bin", "size": len(data), "md5": hashlib.md5(data).hexdigest(), "sha256": hashlib.sha256(data).hexdigest()}
    (tmp_path / "manifest.json").write_text(json.dumps({"version": FW_LATEST, "variants": variants}))
    monkeypatch.setattr(firmware, "FIRMWARE_DIR", tmp_path)
    return {"files": files, "variants": variants}


@pytest.fixture(autouse=True)
async def integration_first(hass, auto_enable_custom_integrations, fake_firmware):
    """Set the integration (and its frontend dependency) up before any test client freezes the HTTP router.

    Home Assistant lifts that freeze at runtime; the test server does not, so registering the card's static path
    later would fail only in the tests."""
    assert await async_setup_component(hass, DOMAIN, {})


async def make_token(hass, *, long_lived: bool = True, name: str = "panel", admin: bool = False) -> str:
    user = await hass.auth.async_create_user(name, group_ids=["system-admin" if admin else "system-users"])
    kwargs = (
        {"token_type": TOKEN_TYPE_LONG_LIVED_ACCESS_TOKEN, "access_token_expiration": timedelta(days=3650)}
        if long_lived
        else {"client_id": "https://ha.test/"}
    )
    refresh = await hass.auth.async_create_refresh_token(user, client_name=name, **kwargs)
    return hass.auth.async_create_access_token(refresh)


def device(name: str, brightness: int = 80, key: str | None = None, rotation: int = 0) -> ConfigSubentryData:
    return ConfigSubentryData(
        data={CONF_BRIGHTNESS: brightness, CONF_ROTATION: rotation, CONF_DEVICE_KEY: key or f"key-{name.lower()}"},
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
