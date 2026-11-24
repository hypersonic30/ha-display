"""Tone sequences for the panels' buzzer, and the `display.beep` service."""
from __future__ import annotations

from typing import Any

from .const import (
    BEEP_DEFAULT_HZ,
    BEEP_DEFAULT_MS,
    BEEP_GAP_MS,
    BEEP_MAX_HZ,
    BEEP_MAX_REPEAT,
    BEEP_MAX_STEP_MS,
    BEEP_MAX_STEPS,
    BEEP_MAX_TOTAL_MS,
    BEEP_MIN_HZ,
    BEEP_MIN_MS,
    BEEP_PRESETS,
)


def _number(value: Any, default: int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or value != value:
        return default
    return int(value)


def clamp_tones(tones: list[list[int]]) -> list[list[int]]:
    """What a panel may be sent: at most 16 steps, a step at most 2 s, all together at most 5 s, a frequency of
    0 (pause) or 100..5000 Hz. Steps without a duration are dropped, the sequence is cut where the total runs out."""
    out: list[list[int]] = []
    total = 0
    for hz, ms in tones:
        if len(out) >= BEEP_MAX_STEPS:
            break
        ms = min(ms, BEEP_MAX_STEP_MS)
        if ms <= 0:
            continue
        ms = min(ms, BEEP_MAX_TOTAL_MS - total)
        if ms <= 0:
            break
        hz = 0 if hz <= 0 else max(BEEP_MIN_HZ, min(BEEP_MAX_HZ, hz))
        out.append([hz, ms])
        total += ms
    return out


def build_tones(preset: str | None = None, frequency: Any = None, duration_ms: Any = None, repeat: Any = None) -> list[list[int]]:
    """The tone list for one `display.beep` call. A preset wins over frequency and duration; repeat applies to both."""
    if preset:
        base = [list(step) for step in BEEP_PRESETS.get(preset, [])]
    else:
        hz = max(BEEP_MIN_HZ, min(BEEP_MAX_HZ, _number(frequency, BEEP_DEFAULT_HZ)))
        ms = max(BEEP_MIN_MS, min(BEEP_MAX_STEP_MS, _number(duration_ms, BEEP_DEFAULT_MS)))
        base = [[hz, ms]]
    times = max(1, min(BEEP_MAX_REPEAT, _number(repeat, 1)))
    tones: list[list[int]] = []
    for i in range(times):
        if i:
            tones.append([0, BEEP_GAP_MS])
        tones.extend(base)
    return clamp_tones(tones)
