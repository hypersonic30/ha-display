from __future__ import annotations

from pathlib import Path

from homeassistant.components import panel_custom
from homeassistant.components.frontend import add_extra_js_url, async_remove_panel
from homeassistant.components.http import StaticPathConfig
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import Event, HomeAssistant
from homeassistant.helpers import config_validation as cv
from homeassistant.loader import async_get_integration
from homeassistant.helpers.typing import ConfigType

from . import services, websocket_api
from .const import CARD_FILE, CONF_DEVICE_KEY, CONF_SIDEBAR, DOMAIN, PANEL_FILE, PANEL_URL, STATIC_PATH
from .devices import device_subentries, rotation_of, screen_size
from .firmware import DisplayFirmwareView, async_load_bundle
from .layouts import LayoutStore
from .panel import CLOSE_GONE, DisplayPingView, DisplaySocketView

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)
PLATFORMS = ["binary_sensor", "update"]


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Runs once per start: the card, the layout store and the WebSocket commands."""
    integration = await async_get_integration(hass, DOMAIN)
    www = str(Path(__file__).parent / "www")
    await hass.http.async_register_static_paths([StaticPathConfig(STATIC_PATH, www, cache_headers=False)])
    # The version in the URL makes browsers pick up a new card after an update.
    add_extra_js_url(hass, f"{STATIC_PATH}/{CARD_FILE}?v={integration.version}")

    store = LayoutStore(hass)
    await store.async_load()
    hass.data[DOMAIN] = {"store": store, "sessions": set(), "firmware": await async_load_bundle(hass)}
    websocket_api.async_register(hass)
    services.async_setup_services(hass)
    hass.http.register_view(DisplaySocketView(hass))
    hass.http.register_view(DisplayPingView())
    hass.http.register_view(DisplayFirmwareView(hass))

    async def close_panels(_: Event) -> None:
        for session in list(hass.data[DOMAIN]["sessions"]):
            await session.close(1001, "shutdown")

    hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, close_panels)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    store: LayoutStore = hass.data[DOMAIN]["store"]
    devices = device_subentries(entry)
    await store.async_prune({sub.subentry_id for sub in devices})
    for sub in devices:  # a changed orientation: everything has to fit the new screen
        await store.async_fit(sub.subentry_id, screen_size(rotation_of(sub)))
    # A removed display, or one whose device key was renewed, must not stay connected.
    keys = {sub.subentry_id: sub.data[CONF_DEVICE_KEY] for sub in devices}
    for session in list(hass.data[DOMAIN]["sessions"]):
        if keys.get(session.device_id) != session.key:
            await session.close(CLOSE_GONE, "gone")

    if entry.data.get(CONF_SIDEBAR, True) and PANEL_URL not in hass.data.get("frontend_panels", {}):
        integration = await async_get_integration(hass, DOMAIN)
        await panel_custom.async_register_panel(
            hass,
            webcomponent_name="display-panel",
            frontend_url_path=PANEL_URL,
            module_url=f"{STATIC_PATH}/{PANEL_FILE}?v={integration.version}",
            sidebar_title="ESP32 Display",
            sidebar_icon="mdi:monitor-edit",
            require_admin=True,
            config={},
        )
        hass.data[DOMAIN]["sidebar"] = True

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_reload))
    # Panels that stay connected across a reload get changed settings (e.g. brightness) right away.
    for sub in devices:
        store.notify(sub.subentry_id)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    if hass.data[DOMAIN].pop("sidebar", False):
        async_remove_panel(hass, PANEL_URL)
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: ConfigEntry) -> None:
    for session in list(hass.data[DOMAIN]["sessions"]):
        await session.close(CLOSE_GONE, "gone")
    await hass.data[DOMAIN]["store"].async_clear()


async def _reload(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
