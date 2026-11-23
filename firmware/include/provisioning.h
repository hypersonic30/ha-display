#pragma once
#include "storage.h"

void provisioning_begin(const char *ap_ssid);
void provisioning_handle();
bool provisioning_done();
Config provisioning_config();
void provisioning_end();
