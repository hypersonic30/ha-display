from homeassistant import config_entries
from homeassistant.config_entries import ConfigEntryState
from homeassistant.data_entry_flow import FlowResultType

from custom_components.display.const import CONF_BRIGHTNESS, CONF_HA_URL, CONF_NAME, CONF_TOKEN, DOMAIN, SUBENTRY_DEVICE

from .conftest import device, make_token


async def _start(hass):
    return await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})


async def test_one_connection_with_one_token(hass, token):
    result = await _start(hass)
    assert result["type"] is FlowResultType.FORM and result["step_id"] == "user"
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_HA_URL: " http://192.168.1.10:8123/ ", CONF_TOKEN: f" {token} "}
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"] == {CONF_HA_URL: "http://192.168.1.10:8123", CONF_TOKEN: token}
    assert result["title"] == "ESP32 Display"


async def test_bad_input_is_reported_per_field(hass, token):
    short_lived = await make_token(hass, long_lived=False, name="browser")
    for url, tok, field, error in (
        ("homeassistant.local", token, CONF_HA_URL, "invalid_url"),
        ("ftp://ha:21", token, CONF_HA_URL, "invalid_url"),
        ("http://ha:8123", "garbage", CONF_TOKEN, "invalid_auth"),
        ("http://ha:8123", short_lived, CONF_TOKEN, "not_long_lived"),
    ):
        result = await _start(hass)
        result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HA_URL: url, CONF_TOKEN: tok})
        assert result["type"] is FlowResultType.FORM
        assert result["errors"] == {field: error}


async def test_second_connection_is_refused(hass, make_entry):
    make_entry()
    result = await _start(hass)
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "single_instance_allowed"


async def test_devices_are_subentries_with_unique_names(hass, make_entry):
    entry = make_entry(device("Küche"))
    flow = hass.config_entries.subentries
    result = await flow.async_init((entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_USER})
    assert result["type"] is FlowResultType.FORM

    # 'kuche ' is the same panel as 'Küche' (the panel's typed name is matched the same way)
    clash = await flow.async_configure(result["flow_id"], {CONF_NAME: " kuche ", CONF_BRIGHTNESS: 50})
    assert clash["errors"] == {CONF_NAME: "name_taken"}
    empty = await flow.async_configure(result["flow_id"], {CONF_NAME: "  ", CONF_BRIGHTNESS: 50})
    assert empty["errors"] == {CONF_NAME: "name_invalid"}
    long = await flow.async_configure(result["flow_id"], {CONF_NAME: "ü" * 20, CONF_BRIGHTNESS: 50})
    assert long["errors"] == {CONF_NAME: "name_too_long"}

    ok = await flow.async_configure(result["flow_id"], {CONF_NAME: "Flur", CONF_BRIGHTNESS: 40})
    assert ok["type"] is FlowResultType.CREATE_ENTRY
    titles = sorted(s.title for s in entry.subentries.values())
    assert titles == ["Flur", "Küche"]
    flur = next(s for s in entry.subentries.values() if s.title == "Flur")
    assert flur.data == {CONF_BRIGHTNESS: 40} and flur.subentry_type == SUBENTRY_DEVICE


async def test_rename_keeps_the_device_and_its_own_name_is_not_a_clash(hass, make_entry):
    entry = make_entry(device("Küche"), device("Flur"))
    sub = next(s for s in entry.subentries.values() if s.title == "Küche")
    flow = hass.config_entries.subentries
    result = await flow.async_init((entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_RECONFIGURE, "subentry_id": sub.subentry_id})
    same = await flow.async_configure(result["flow_id"], {CONF_NAME: "KÜCHE", CONF_BRIGHTNESS: 25})
    assert same["type"] is FlowResultType.ABORT and same["reason"] == "reconfigure_successful"
    assert entry.subentries[sub.subentry_id].title == "KÜCHE"
    assert entry.subentries[sub.subentry_id].data[CONF_BRIGHTNESS] == 25


async def test_revoked_token_starts_reauth_and_a_new_token_fixes_it(hass, make_entry, token):
    entry = make_entry(device("Küche"), tok="not-a-valid-token")
    await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_ERROR
    flows = list(entry.async_get_active_flows(hass, {config_entries.SOURCE_REAUTH}))
    assert len(flows) == 1

    result = await hass.config_entries.flow.async_configure(flows[0]["flow_id"], {CONF_TOKEN: "still-bad"})
    assert result["errors"] == {CONF_TOKEN: "invalid_auth"}
    result = await hass.config_entries.flow.async_configure(flows[0]["flow_id"], {CONF_TOKEN: token})
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "reauth_successful"
    await hass.async_block_till_done()
    assert entry.data[CONF_TOKEN] == token and entry.state is ConfigEntryState.LOADED
