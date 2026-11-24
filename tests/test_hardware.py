"""Hardware settings owned by Home Assistant: the checks, the form and what goes to the panel."""
import shutil
import subprocess
from pathlib import Path

import pytest
from homeassistant import config_entries
from homeassistant.components.websocket_api import ActiveConnection  # noqa: F401
from homeassistant.config_entries import ConfigSubentryData
from homeassistant.data_entry_flow import FlowResultType

from custom_components.display import hw
from custom_components.display.const import (
    CONF_BRIGHTNESS,
    CONF_DEVICE_KEY,
    CONF_HW,
    CONF_HW_MANAGE,
    CONF_NAME,
    CONF_ROTATION,
    SUBENTRY_DEVICE,
)
from custom_components.display.devices import hw_of

from .conftest import device
from .test_panel import connect, recv, setup

ROOT = Path(__file__).resolve().parents[1]


def with_pins(**changes):
    return {**hw.DEFAULTS, **changes}


# ── the checks (mirror firmware/src/hw.cpp) ───────────────────────────────────

def test_the_authors_board_and_every_controller_preset_are_valid():
    assert hw.validate(hw.DEFAULTS) is None
    for panel in range(len(hw.PANELS)):
        assert hw.validate(hw.preset(hw.DEFAULTS, panel)) is None


@pytest.mark.parametrize("changes, error", [
    ({"mosi": 6}, "hw_pin_unusable"),        # flash
    ({"mosi": 1}, "hw_pin_unusable"),        # UART0
    ({"mosi": 0}, "hw_pin_unusable"),        # BOOT
    ({"mosi": 24}, "hw_pin_unusable"),       # does not exist
    ({"mosi": 40}, "hw_pin_unusable"),
    ({"mosi": 34}, "hw_pin_input_only"),
    ({"dc": 15}, "hw_pin_duplicate"),        # same as CS
    ({"touch_cs": 15}, "hw_pin_duplicate"),
    ({"cs": -1}, "hw_pin_missing"),
    ({"write_mhz": 0}, "hw_clock"),
    ({"write_mhz": 41}, "hw_clock"),
    ({"read_mhz": 21}, "hw_clock"),
    ({"panel": 9}, "hw_panel"),
])
def test_bad_sets_are_refused(changes, error):
    bad = hw.validate(with_pins(**changes))
    assert bad is not None and bad[0] == error


@pytest.mark.parametrize("changes", [
    {"miso": 34}, {"touch_irq": 35}, {"touch_irq": 27}, {"rst": -1}, {"bl": -1}, {"buzzer": -1},
    {"touch_on": 0, "touch_cs": -1},
])
def test_allowed_variations_pass(changes):
    assert hw.validate(with_pins(**changes)) is None


def test_defaults_match_the_firmware(tmp_path):
    """The numbers here and the ones in firmware/src/hw.cpp must not drift apart: compile the firmware's
    defaults (ili9481 build) and compare."""
    gxx = shutil.which("g++")
    libs = ROOT / "firmware" / ".pio" / "libdeps" / "ili9481" / "ArduinoJson" / "src"
    if not gxx or not libs.is_dir():
        pytest.skip("g++ or the PlatformIO libraries are not available")
    src = tmp_path / "defaults.cpp"
    src.write_text('#include "hw_json.h"\n#include <cstdio>\n#include <string>\nint main(){HwConfig c;hw_defaults(c);JsonDocument d;'
                   'hw_to_json(c,d.to<JsonObject>());std::string o;serializeJson(d,o);puts(o.c_str());}\n')
    exe = tmp_path / "defaults"
    fw = ROOT / "firmware"
    subprocess.run([gxx, "-std=c++17", "-I", str(fw / "tests" / "hw" / "shim"), "-I", str(fw / "include"), "-I", str(libs),
                    str(src), str(fw / "src" / "hw_json.cpp"), str(fw / "src" / "hw.cpp"), "-o", str(exe)], check=True)
    import json  # noqa: PLC0415
    assert json.loads(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout) == hw.DEFAULTS


# ── the form ──────────────────────────────────────────────────────────────────

async def _reconfigure(hass, entry, sub, **values):
    flow = hass.config_entries.subentries
    result = await flow.async_init(
        (entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_RECONFIGURE, "subentry_id": sub.subentry_id}
    )
    return flow, await flow.async_configure(result["flow_id"], {CONF_NAME: "Küche", CONF_BRIGHTNESS: 25, CONF_ROTATION: "0", **values})


def _hw_form(**changes):
    form = {"panel": "ili9481", "use_preset": False, "bl_invert": False, "bgr": True, "invert": True, "touch_on": True,
            **{k: v for k, v in hw.DEFAULTS.items() if k in hw.PIN_KEYS + ("write_mhz", "read_mhz")}}
    form.update(changes)
    return form


async def test_without_the_switch_the_form_has_no_hardware_step(hass, make_entry):
    entry = make_entry(device("Küche"))
    sub = next(iter(entry.subentries.values()))
    _, done = await _reconfigure(hass, entry, sub)
    assert done["type"] is FlowResultType.ABORT
    assert CONF_HW_MANAGE not in entry.subentries[sub.subentry_id].data and hw_of(entry.subentries[sub.subentry_id]) is None


async def test_hardware_is_stored_when_home_assistant_manages_it(hass, make_entry):
    entry = make_entry(device("Küche"))
    sub = next(iter(entry.subentries.values()))
    flow, step = await _reconfigure(hass, entry, sub, **{CONF_HW_MANAGE: True})
    assert step["type"] is FlowResultType.FORM and step["step_id"] == "hardware"
    # the form starts with the author's board
    assert {str(k): k.default() for k in step["data_schema"].schema if str(k) in ("mosi", "write_mhz")} == {"mosi": 23, "write_mhz": 10}

    done = await flow.async_configure(step["flow_id"], _hw_form(write_mhz=5, touch_irq=27))
    assert done["type"] is FlowResultType.ABORT and done["reason"] == "reconfigure_successful"
    stored = entry.subentries[sub.subentry_id]
    assert stored.data[CONF_HW_MANAGE] is True
    assert hw_of(stored) == with_pins(write_mhz=5, touch_irq=27)
    assert stored.data[CONF_DEVICE_KEY] == "key-küche"          # the rest of the display is untouched


async def test_a_bad_set_keeps_the_form_open_and_says_why(hass, make_entry):
    entry = make_entry(device("Küche"))
    sub = next(iter(entry.subentries.values()))
    flow, step = await _reconfigure(hass, entry, sub, **{CONF_HW_MANAGE: True})
    again = await flow.async_configure(step["flow_id"], _hw_form(dc=15))       # same pin as CS
    assert again["type"] is FlowResultType.FORM and again["errors"] == {"base": "hw_pin_duplicate"}
    assert again["description_placeholders"]["gpio"] == "15"
    assert hw_of(entry.subentries[sub.subentry_id]) is None                    # nothing stored yet


async def test_the_controller_preset_replaces_clock_and_colours(hass, make_entry):
    entry = make_entry(device("Küche"))
    sub = next(iter(entry.subentries.values()))
    flow, step = await _reconfigure(hass, entry, sub, **{CONF_HW_MANAGE: True})
    await flow.async_configure(step["flow_id"], _hw_form(panel="ili9488", use_preset=True, write_mhz=3, bgr=False, invert=True))
    stored = hw_of(entry.subentries[sub.subentry_id])
    assert (stored["panel"], stored["write_mhz"], stored["read_mhz"], stored["bgr"], stored["invert"]) == (1, 20, 10, 1, 0)


async def test_switching_it_off_forgets_the_set(hass, make_entry):
    managed = ConfigSubentryData(
        data={CONF_BRIGHTNESS: 80, CONF_ROTATION: 0, CONF_DEVICE_KEY: "key-küche", CONF_HW_MANAGE: True, CONF_HW: with_pins(write_mhz=5)},
        subentry_type=SUBENTRY_DEVICE, title="Küche", unique_id="küche")
    entry = make_entry(managed)
    sub = next(iter(entry.subentries.values()))
    assert hw_of(sub)["write_mhz"] == 5
    flow, step = await _reconfigure(hass, entry, sub, **{CONF_HW_MANAGE: False})
    assert step["type"] is FlowResultType.ABORT
    assert CONF_HW not in entry.subentries[sub.subentry_id].data and hw_of(entry.subentries[sub.subentry_id]) is None


async def test_a_new_display_can_start_with_managed_hardware(hass, make_entry):
    entry = make_entry(device("Küche"))
    flow = hass.config_entries.subentries
    result = await flow.async_init((entry.entry_id, SUBENTRY_DEVICE), context={"source": config_entries.SOURCE_USER})
    step = await flow.async_configure(result["flow_id"], {CONF_NAME: "Flur", CONF_BRIGHTNESS: 50, CONF_HW_MANAGE: True})
    assert step["type"] is FlowResultType.FORM and step["step_id"] == "hardware"
    done = await flow.async_configure(step["flow_id"], _hw_form(bl=-1))
    assert done["type"] is FlowResultType.CREATE_ENTRY
    assert hw_of(next(s for s in entry.subentries.values() if s.title == "Flur"))["bl"] == -1


# ── what the panel receives ───────────────────────────────────────────────────

async def test_only_managed_displays_get_a_hardware_section(hass, make_entry, hass_client_no_auth):
    managed = ConfigSubentryData(
        data={CONF_BRIGHTNESS: 80, CONF_ROTATION: 0, CONF_DEVICE_KEY: "key-küche", CONF_HW_MANAGE: True, CONF_HW: with_pins(write_mhz=5)},
        subentry_type=SUBENTRY_DEVICE, title="Küche", unique_id="küche")
    await setup(hass, make_entry, managed, device("Bad"))
    layout = await recv(await connect(hass_client_no_auth))
    assert layout["settings"]["hw"] == with_pins(write_mhz=5)

    assert "hw" not in (await recv(await connect(hass_client_no_auth, key="key-bad")))["settings"]
