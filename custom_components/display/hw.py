"""Hardware settings of a display (controller, pins, clocks) that Home Assistant can own.

Mirrors firmware/src/hw.cpp: the defaults are the author's board, the checks are the ones the firmware runs
before it accepts a set (so a set that passes here is accepted there). The keys are the ones of the portal's
hardware section and of `settings.hw` in the layout frame (docs/protocol.md).
"""
from __future__ import annotations

from typing import Any

PANELS = ("ili9481", "ili9488", "ili9486", "st7796")     # index = the firmware's PanelKind

# Recommended clock / colour flags per controller: (write MHz, read MHz, BGR, invert)
PRESETS: dict[int, tuple[int, int, int, int]] = {
    0: (10, 4, 1, 1),     # verified on the author's panel
    1: (20, 10, 1, 0),
    2: (20, 10, 1, 1),
    3: (20, 10, 1, 0),
}

PIN_KEYS = ("mosi", "sclk", "miso", "cs", "dc", "rst", "bl", "touch_cs", "touch_irq", "buzzer")
FLAG_KEYS = ("bl_invert", "touch_on", "bgr", "invert")

DEFAULTS: dict[str, int] = {
    "panel": 0,
    "mosi": 23, "sclk": 18, "miso": 19,
    "cs": 15, "dc": 2, "rst": 4,
    "bl": 32, "bl_invert": 0,
    "write_mhz": 10, "read_mhz": 4,
    "touch_on": 1, "touch_cs": 14, "touch_irq": -1,
    "bgr": 1, "invert": 1,
    "buzzer": 26,
}


def preset(config: dict[str, int], panel: int) -> dict[str, int]:
    """`config` with the controller's recommended clock and colour flags."""
    write, read, bgr, invert = PRESETS[panel]
    return {**config, "panel": panel, "write_mhz": write, "read_mhz": read, "bgr": bgr, "invert": invert}


def _gpio_exists(p: int) -> bool:
    # ESP32-WROOM-32D: usable GPIOs, minus flash (6-11), UART0 (1, 3) and the BOOT strap (0)
    return 2 <= p <= 5 or 12 <= p <= 19 or 21 <= p <= 23 or 25 <= p <= 27 or 32 <= p <= 39


def _gpio_output(p: int) -> bool:
    return _gpio_exists(p) and p < 34


def validate(c: dict[str, Any]) -> tuple[str, dict[str, Any]] | None:
    """None when the set is fine, else (error key, placeholders for the message)."""
    if c["panel"] not in range(len(PANELS)):
        return "hw_panel", {}
    if not 1 <= c["write_mhz"] <= 40:
        return "hw_clock", {"what": "Schreibtakt", "max": 40}
    if not 1 <= c["read_mhz"] <= 20:
        return "hw_clock", {"what": "Lesetakt", "max": 20}
    # (key, name, optional, input-only pins allowed)
    pins = [
        ("mosi", "MOSI", False, False), ("sclk", "SCLK", False, False), ("miso", "MISO", False, True),
        ("cs", "CS", False, False), ("dc", "DC", False, False), ("rst", "RST", True, False),
        ("bl", "Backlight", True, False),
    ]
    if c["touch_on"]:
        pins += [("touch_cs", "Touch-CS", False, False), ("touch_irq", "Touch-IRQ", True, True)]
    pins.append(("buzzer", "Summer", True, False))
    seen: dict[int, str] = {}
    for key, name, optional, input_ok in pins:
        pin = c[key]
        if pin < 0:
            if not optional:
                return "hw_pin_missing", {"pin": name}
            continue
        if not _gpio_exists(pin):
            return "hw_pin_unusable", {"pin": name, "gpio": pin}
        if not input_ok and not _gpio_output(pin):
            return "hw_pin_input_only", {"pin": name, "gpio": pin}
        if pin in seen:
            return "hw_pin_duplicate", {"pin": name, "other": seen[pin], "gpio": pin}
        seen[pin] = name
    return None
