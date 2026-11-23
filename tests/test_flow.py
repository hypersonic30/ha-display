from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResultType

from custom_components.display.const import CONF_BRIGHTNESS, CONF_DEVICE_KEY, CONF_HA_URL, CONF_NAME, CONF_NEW_KEY, DOMAIN, SUBENTRY_DEVICE

from .conftest import device


async def _start(hass):
    return await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})


async def test_the_connection_needs_only_the_address_no_token(hass):
    result = await _start(hass)
    assert result["type"] is FlowResultType.FORM and list(result["data_schema"].schema) == [CONF_HA_URL]
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HA_URL: " http://192.168.1.10:8123/ "})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"] == {CONF_HA_URL: "http://192.168.1.10:8123"}


async def test_bad_address_is_reported(hass):
    for url in ("homeassistant.local", "ftp://ha:21", "http://"):
        result = await _start(hass)
        result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HA_URL: url})
        assert result["type"] is FlowResultType.FORM and result["errors"] == {CONF_HA_URL: "invalid_url"}, url


async def test_second_connection_is_refused(hass, make_entry):
    make_entry()
    result = await _start(hass)
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "single_instance_allowed"


async def test_address_can_be_changed(hass, make_entry):
    entry = make_entry()
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_RECONFIGURE, "entry_id": entry.entry_id}
    )
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HA_URL: "https://ha.example.org/"})
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "reconfigure_successful"
    assert entry.data == {CONF_HA_URL: "https://ha.example.org"}


async def _new_display(hass, entry, name, brightness=50):
    flow = hass.config_entries.subentries
    result = await flow.async_init((entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_USER})
    return result, await flow.async_configure(result["flow_id"], {CONF_NAME: name, CONF_BRIGHTNESS: brightness})


async def test_every_display_gets_its_own_unguessable_device_key(hass, make_entry):
    entry = make_entry(device("Küche"))
    keys = set()
    for name in ("Flur", "Bad"):
        _, ok = await _new_display(hass, entry, name)
        assert ok["type"] is FlowResultType.CREATE_ENTRY
        keys.add(next(s for s in entry.subentries.values() if s.title == name).data[CONF_DEVICE_KEY])
    assert len(keys) == 2 and all(len(k) >= 30 for k in keys) and "key-küche" not in keys


async def test_display_names_must_be_usable_and_unique(hass, make_entry):
    entry = make_entry(device("Küche"))
    for name, error in ((" kuche ", "name_taken"), ("  ", "name_invalid"), ("ü" * 20, "name_too_long")):
        result, again = await _new_display(hass, entry, name)
        assert again["errors"] == {CONF_NAME: error}, name
    assert [s.title for s in entry.subentries.values()] == ["Küche"]


async def test_editing_a_display_keeps_its_key_unless_renewed(hass, make_entry):
    entry = make_entry(device("Küche", key="old-key"))
    sub = next(iter(entry.subentries.values()))
    flow = hass.config_entries.subentries

    async def reconfigure(**values):
        result = await flow.async_init(
            (entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_RECONFIGURE, "subentry_id": sub.subentry_id}
        )
        return await flow.async_configure(result["flow_id"], {CONF_NAME: "KÜCHE", CONF_BRIGHTNESS: 25, **values})

    done = await reconfigure(**{CONF_NEW_KEY: False})
    assert done["type"] is FlowResultType.ABORT and done["reason"] == "reconfigure_successful"
    assert entry.subentries[sub.subentry_id].title == "KÜCHE"
    assert entry.subentries[sub.subentry_id].data == {CONF_BRIGHTNESS: 25, CONF_DEVICE_KEY: "old-key"}

    await reconfigure(**{CONF_NEW_KEY: True})
    fresh = entry.subentries[sub.subentry_id].data[CONF_DEVICE_KEY]
    assert fresh != "old-key" and len(fresh) >= 30
