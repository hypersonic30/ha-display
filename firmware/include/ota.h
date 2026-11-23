#pragma once
#include <Arduino.h>
#include "storage.h"

// Firmware update over the integration connection.
//
// The integration sends {"type":"ota","version","path","size","md5"}. ota_request() only validates and
// notes it (it runs inside the WebSocket callback); the blocking download runs from loop() via ota_run().

// Boot-time software rollback; call first thing in setup().
void ota_boot_check();

// Call from loop(): marks a freshly updated image as good once it has run long enough with a network.
void ota_loop_tick(bool network_ok);

void ota_request(const char *version, const char *path, uint32_t size, const char *md5);

bool ota_pending();            // a validated request waits for ota_run()
bool ota_running();            // ota_run() is in progress (layout frames must not touch the screen)

// Download + flash. Returns only on failure (the device restarts on success); the reason is in ota_last_error().
bool ota_run(const Config &cfg);
const char *ota_last_error();
