#pragma once
#include <stdint.h>

// Tone sequences for the buzzer. Pure helpers (no Arduino dependency) so the limits can be tested on the host.
//
// A sequence is a list of steps {hz, ms}; hz == 0 is a pause. The server already clamps what it sends, this is
// the last line of defence on the device: whatever arrives in a `beep` frame, the buzzer never runs longer than
// BEEP_MAX_TOTAL_MS and never outside the audible range of the transducer.
constexpr int BEEP_MAX_STEPS    = 16;
constexpr int BEEP_MAX_STEP_MS  = 2000;
constexpr int BEEP_MAX_TOTAL_MS = 5000;
constexpr int BEEP_MIN_HZ       = 100;
constexpr int BEEP_MAX_HZ       = 5000;

struct BeepStep {
    uint16_t hz;   // 0 = pause
    uint16_t ms;
};

// Takes `n` raw (hz, ms) pairs, writes at most BEEP_MAX_STEPS clean steps to `out` and returns how many:
//  - steps without a positive duration are dropped, a duration is cut to BEEP_MAX_STEP_MS,
//  - a frequency is 0 (pause) or clamped into BEEP_MIN_HZ..BEEP_MAX_HZ,
//  - the sum of all durations is cut to BEEP_MAX_TOTAL_MS (the last step is shortened, the rest dropped).
inline int beep_clamp(const long *hz, const long *ms, int n, BeepStep *out) {
    int count = 0;
    long total = 0;
    for (int i = 0; i < n && count < BEEP_MAX_STEPS; i++) {
        long d = ms[i];
        if (d <= 0) continue;
        if (d > BEEP_MAX_STEP_MS) d = BEEP_MAX_STEP_MS;
        if (total + d > BEEP_MAX_TOTAL_MS) d = BEEP_MAX_TOTAL_MS - total;
        if (d <= 0) break;
        long f = hz[i];
        if (f > 0) f = f < BEEP_MIN_HZ ? BEEP_MIN_HZ : (f > BEEP_MAX_HZ ? BEEP_MAX_HZ : f);
        else f = 0;
        out[count].hz = (uint16_t)f;
        out[count].ms = (uint16_t)d;
        count++;
        total += d;
    }
    return count;
}
