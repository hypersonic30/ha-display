#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

// TLS trust for https:// Home Assistant URLs.
//
// verify = true : the server certificate chain is checked against the built-in Mozilla root bundle of the
//                 ESP-IDF (about 130 roots, linked into the image) and the host name has to match.
//                 Needs a correct clock: see tls_sync_clock().
// verify = false: encrypted, but nothing is authenticated (self-signed certificates in the LAN).
void tls_configure(WiFiClientSecure &client, bool verify);

// Certificates are only valid inside a time window and the device has no RTC. Sets the system clock once per
// boot: first NTP (pool.ntp.org, 4 s), then, if UDP 123 is blocked, the Date header of the Home Assistant
// server itself (read over an unauthenticated TLS connection; it only sets the clock, the real connection is
// verified afterwards). False = no usable time, verification cannot work.
bool tls_sync_clock(const char *host, uint16_t port);

// Short German reason of the last failed TLS step ("" if none), for the status screen.
const char *tls_last_error();
void        tls_set_error(const char *text);

// Bundle used by the WebSockets library (patched, see tools/patch_ws.py): marker for "built-in bundle".
#define TLS_BUILTIN_BUNDLE ((const uint8_t *)1)

// Heap figures for the log: tls_log_heap("before handshake").
void tls_log_heap(const char *what);
