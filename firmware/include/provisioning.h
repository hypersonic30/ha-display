#pragma once
#include "storage.h"

// Starts a WPA2 hotspot with a random password (see provisioning_ap_pass()).
void provisioning_begin(const char *ap_ssid);
const char *provisioning_ap_pass();
void provisioning_handle();
bool provisioning_done();
Config provisioning_config();
void provisioning_end();
