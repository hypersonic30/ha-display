#pragma once
#include "storage.h"

// Starts a WPA2 hotspot with a random password (see provisioning_ap_pass()) and the setup page.
// `current` (may be null) is what the page shows as saved: WLAN names, Home Assistant URL, whether a
// device key exists. Passwords and the key are never sent to the browser.
void provisioning_begin(const char *ap_ssid, const Config *current);
const char *provisioning_ap_pass();
void provisioning_handle();
bool provisioning_done();
Config provisioning_config();
void provisioning_end();

// "Touch neu kalibrieren" from the setup page: the POST only sets a request, main.cpp runs the (blocking)
// calibration on the display between two portal passes and reports the outcome back for the page to poll.
bool provisioning_calibrate_requested();       // true once per request
void provisioning_calibrate_state(int state);  // 0 idle, 1 running, 2 stored, 3 nobody touched the screen
