#include "tls.h"
#include <HTTPClient.h>
#include <sys/time.h>
#include <time.h>

static char last_error[96] = "";

const char *tls_last_error() { return last_error; }
void tls_set_error(const char *text) { strlcpy(last_error, text ? text : "", sizeof(last_error)); }

void tls_log_heap(const char *what) {
    Serial.printf("[TLS] heap %s: free %u, largest block %u, minimum since boot %u\n", what,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
}

void tls_configure(WiFiClientSecure &client, bool verify) {
    if (verify) client.useBuiltinCACertBundle();
    else        client.setInsecure();
}

// Anything after 2024-01-01 counts as a set clock (the ESP32 starts at 1970).
static bool clock_set() { return time(nullptr) > 1704067200; }

static int month_index(const char *m) {
    static const char *const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) if (strncmp(m, names[i], 3) == 0) return i;
    return -1;
}

// "Tue, 24 Nov 2026 07:30:00 GMT" -> epoch seconds (UTC), 0 if it does not parse. Own date maths: no
// dependence on the TZ setting.
static time_t parse_http_date(const String &d) {
    int day, year, hh, mm, ss;
    char mon[4] = {0};
    if (sscanf(d.c_str(), "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) return 0;
    int m = month_index(mon);
    if (m < 0 || year < 2024 || day < 1 || day > 31) return 0;
    int y = year - (m < 2 ? 1 : 0);
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 1 ? -2 : 10)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    return (time_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

static bool sync_from_http_date(const char *host, uint16_t port) {
    WiFiClientSecure secure;
    secure.setInsecure();          // only the Date header is wanted here
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(5000);
    char url[160];
    snprintf(url, sizeof(url), "https://%s:%u/", host, (unsigned)port);
    if (!http.begin(secure, url)) return false;
    const char *keys[] = {"Date"};
    http.collectHeaders(keys, 1);
    http.GET();                    // any status: the header is there
    String date = http.header("Date");
    http.end();
    time_t t = parse_http_date(date);
    if (!t) return false;
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    return true;
}

bool tls_sync_clock(const char *host, uint16_t port) {
    if (clock_set()) return true;
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    for (uint32_t t0 = millis(); !clock_set() && millis() - t0 < 4000;) delay(100);
    if (clock_set()) {
        Serial.printf("[TLS] clock set by NTP: %lu\n", (unsigned long)time(nullptr));
        return true;
    }
    Serial.println("[TLS] NTP not reachable, trying the Date header of the Home Assistant server");
    if (sync_from_http_date(host, port) && clock_set()) {
        Serial.printf("[TLS] clock set from the server's Date header: %lu\n", (unsigned long)time(nullptr));
        return true;
    }
    tls_set_error("Uhrzeit nicht verfügbar (für die Zertifikatsprüfung nötig)");
    Serial.println("[TLS] no usable time: certificate validity cannot be checked");
    return false;
}
