#include "buzzer.h"
#include <Arduino.h>

static int8_t   pin      = -1;
static bool     attached = false;
static BeepStep steps[BEEP_MAX_STEPS];
static int      count   = 0;
static int      idx     = 0;
static uint32_t step_end = 0;

// LEDC with 10 bit resolution: ledcWriteTone() switches the channel's timer to 10 bit as well. The backlight
// uses 8 bit, so the two never match and the core gives the buzzer a timer of its own.
static const uint32_t ATTACH_HZ  = 2000;
static const uint8_t  ATTACH_RES = 10;

static void idle_low() {
    if (attached) {
        ledcWrite(pin, 0);
        ledcDetach(pin);
        attached = false;
    }
    if (pin >= 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);    // the transistor base stays off: silent and no current
    }
}

static void start_step() {
    const BeepStep &s = steps[idx];
    if (s.hz == 0) {
        if (attached) ledcWrite(pin, 0);
    } else {
        if (!attached) attached = ledcAttach(pin, ATTACH_HZ, ATTACH_RES);
        if (attached) ledcWriteTone(pin, s.hz);
    }
    step_end = millis() + s.ms;
}

void buzzer_begin(int8_t p) {
    pin = p;
    count = idx = 0;
    attached = false;
    if (pin >= 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        Serial.printf("[BUZZER] pin %d\n", pin);
    }
}

bool buzzer_present() { return pin >= 0; }
bool buzzer_busy()    { return count > 0; }

void buzzer_stop() {
    count = idx = 0;
    idle_low();
}

void buzzer_play(const BeepStep *in, int n) {
    if (pin < 0 || n <= 0) return;
    if (n > BEEP_MAX_STEPS) n = BEEP_MAX_STEPS;
    memcpy(steps, in, n * sizeof(BeepStep));
    count = n;
    idx = 0;
    start_step();
}

void buzzer_play_json(JsonArrayConst tones) {
    if (pin < 0) return;
    long hz[BEEP_MAX_STEPS], ms[BEEP_MAX_STEPS];
    int n = 0;
    for (JsonVariantConst t : tones) {
        if (n >= BEEP_MAX_STEPS) break;
        JsonArrayConst pair = t.as<JsonArrayConst>();
        if (pair.size() < 2) continue;
        hz[n] = pair[0] | 0L;
        ms[n] = pair[1] | 0L;
        n++;
    }
    BeepStep clean[BEEP_MAX_STEPS];
    int m = beep_clamp(hz, ms, n, clean);
    if (m > 0) buzzer_play(clean, m);
}

void buzzer_click() {
    if (pin < 0 || count > 0) return;
    static const BeepStep click = {2500, 20};
    buzzer_play(&click, 1);
}

void buzzer_tick() {
    if (count == 0) return;
    if ((int32_t)(millis() - step_end) < 0) return;
    if (++idx >= count) {
        buzzer_stop();
        return;
    }
    start_step();
}
