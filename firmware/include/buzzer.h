#pragma once
#include <ArduinoJson.h>
#include "beep.h"

// Buzzer on one GPIO (default 26, driven through a transistor): a magnetic transducer needs a tone signal, so
// the pin is a LEDC PWM output while a sequence plays and an ordinary LOW output otherwise. The LEDC timer is
// only attached for the duration of a sequence, so it can never share a timer with the backlight PWM.

void buzzer_begin(int8_t pin);                         // -1 = no buzzer: everything below does nothing
void buzzer_tick();                                    // call from loop(): steps through the running sequence
void buzzer_play(const BeepStep *steps, int n);        // replaces whatever is playing
void buzzer_play_json(JsonArrayConst tones);           // [[hz, ms], ...] as in the `beep` frame (clamped here)
void buzzer_stop();                                    // silence at once, pin LOW
void buzzer_click();                                   // short touch click; ignored while another sound plays
bool buzzer_busy();
bool buzzer_present();
