#include "hw.h"
#include <Preferences.h>
#include <esp_system.h>

#ifndef HW_DEFAULT_PANEL
#define HW_DEFAULT_PANEL PANEL_ILI9481
#endif

static const char *NS = "display";
static const uint8_t HW_VER = 1;

const char *hw_panel_name(uint8_t panel) {
    switch (panel) {
    case PANEL_ILI9481: return "ILI9481";
    case PANEL_ILI9488: return "ILI9488";
    case PANEL_ILI9486: return "ILI9486";
    case PANEL_ST7796:  return "ST7796";
    default:            return "?";
    }
}

void hw_preset(HwConfig &c, uint8_t panel) {
    c.panel = panel;
    c.bgr = 1;
    switch (panel) {
    case PANEL_ILI9481: c.write_mhz = 10; c.read_mhz = 4;  c.invert = 1; break;   // verified on the author's panel
    case PANEL_ILI9488: c.write_mhz = 20; c.read_mhz = 10; c.invert = 0; break;
    case PANEL_ILI9486: c.write_mhz = 20; c.read_mhz = 10; c.invert = 1; break;
    case PANEL_ST7796:  c.write_mhz = 20; c.read_mhz = 10; c.invert = 0; break;
    default: break;
    }
}

void hw_defaults(HwConfig &c) {
    memset(&c, 0, sizeof(c));
    c.ver = HW_VER;
    c.mosi = 23; c.sclk = 18; c.miso = 19;
    c.cs = 15; c.dc = 2; c.rst = 4;
    c.bl = 32; c.bl_invert = 0;
    c.touch_on = 1; c.touch_cs = 14; c.touch_irq = -1;
    hw_preset(c, HW_DEFAULT_PANEL);
}

// ESP32-WROOM-32D: usable GPIOs, minus flash (6-11), UART0 (1, 3) and the BOOT strap (0).
static bool gpio_exists(int p)  { return (p >= 2 && p <= 5) || (p >= 12 && p <= 19) || (p >= 21 && p <= 23) ||
                                         (p >= 25 && p <= 27) || (p >= 32 && p <= 39); }
static bool gpio_output(int p)  { return gpio_exists(p) && p < 34; }

const char *hw_validate(const HwConfig &c) {
    if (c.panel >= PANEL_COUNT) return "Unbekannter Controller";
    if (c.write_mhz < 1 || c.write_mhz > 40) return "Schreibtakt: 1 bis 40 MHz";
    if (c.read_mhz < 1 || c.read_mhz > 20)   return "Lesetakt: 1 bis 20 MHz";

    struct P { int8_t pin; bool optional; bool input_only_ok; const char *name; };
    const P pins[] = {
        {c.mosi, false, false, "MOSI"}, {c.sclk, false, false, "SCLK"}, {c.miso, false, true, "MISO"},
        {c.cs, false, false, "CS"},     {c.dc, false, false, "DC"},     {c.rst, true, false, "RST"},
        {c.bl, true, false, "Backlight"},
        {c.touch_on ? c.touch_cs : (int8_t)-1, !c.touch_on, false, "Touch-CS"},
        {c.touch_on ? c.touch_irq : (int8_t)-1, true, true, "Touch-IRQ"},
    };
    static char msg[64];
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        const P &a = pins[i];
        if (a.pin < 0) {
            if (!a.optional) { snprintf(msg, sizeof(msg), "%s: Pin fehlt", a.name); return msg; }
            continue;
        }
        if (!gpio_exists(a.pin)) {
            snprintf(msg, sizeof(msg), "%s: GPIO %d ist nicht nutzbar (Flash, UART oder BOOT)", a.name, a.pin);
            return msg;
        }
        if (!a.input_only_ok && !gpio_output(a.pin)) {
            snprintf(msg, sizeof(msg), "%s: GPIO %d ist nur Eingang", a.name, a.pin);
            return msg;
        }
        for (size_t j = 0; j < i; j++)
            if (pins[j].pin == a.pin) {
                snprintf(msg, sizeof(msg), "GPIO %d doppelt belegt (%s und %s)", a.pin, pins[j].name, a.name);
                return msg;
            }
    }
    return nullptr;
}

bool hw_load(HwConfig &c) {
    hw_defaults(c);
    Preferences p;
    p.begin(NS, true);
    HwConfig s;
    bool ok = p.getBytesLength("hw_cfg") == sizeof(s) && p.getBytes("hw_cfg", &s, sizeof(s)) == sizeof(s) &&
              s.ver == HW_VER;
    p.end();
    if (!ok) return false;
    if (hw_validate(s)) {
        Serial.printf("[HW] stored configuration rejected (%s), using defaults\n", hw_validate(s));
        return false;
    }
    c = s;
    return true;
}

bool hw_save(const HwConfig &c) {
    if (hw_validate(c)) return false;
    Preferences p;
    p.begin(NS, false);
    HwConfig s = c;
    s.ver = HW_VER;
    bool ok = p.putBytes("hw_cfg", &s, sizeof(s)) == sizeof(s);
    p.end();
    return ok;
}

void hw_reset_stored() {
    Preferences p;
    p.begin(NS, false);
    p.remove("hw_cfg");
    p.end();
}

void hw_print(const HwConfig &c) {
    Serial.printf("[HW] panel %s, SPI write %u MHz read %u MHz, colour %s, inversion %s\n", hw_panel_name(c.panel),
                  c.write_mhz, c.read_mhz, c.bgr ? "BGR" : "RGB", c.invert ? "on" : "off");
    Serial.printf("[HW] MOSI=%d SCLK=%d MISO=%d CS=%d DC=%d RST=%d BL=%d%s, touch %s CS=%d IRQ=%d\n", c.mosi, c.sclk,
                  c.miso, c.cs, c.dc, c.rst, c.bl, c.bl_invert ? " (inverted)" : "", c.touch_on ? "on" : "off",
                  c.touch_cs, c.touch_irq);
}

// ── Recovery ──────────────────────────────────────────────────────────────────

static const uint32_t STABLE_MS   = 20000;   // a boot that lives this long counts as good
static const uint32_t HOLD_MS     = 3000;
static const uint32_t WINDOW_MS   = 30000;   // BOOT key is looked at during this time after power-up
static bool boot_pending = false;            // the stored "this boot has not proven itself yet" mark

bool hw_guard_boot() {
    esp_reset_reason_t r = esp_reset_reason();
    // A crash or watchdog; a plain power cycle, EN key or brown-out is not the configuration's fault.
    bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;

    Preferences p;
    p.begin(NS, false);
    bool     pend = p.getBool("boot_pend", false);
    uint8_t  n    = p.getUChar("boot_fail", 0);
    uint8_t  next = (pend && crash) ? n + 1 : 0;
    bool dropped = false;
    if (next >= 3) {
        p.remove("hw_cfg");
        next = 0;
        dropped = true;
        Serial.println("[HW] 3 crashes in a row right after boot: hardware configuration reset to defaults");
    }
    if (next != n) p.putUChar("boot_fail", next);
    if (!pend)     p.putBool("boot_pend", true);
    p.end();
    boot_pending = true;
    return dropped;
}

void hw_guard_poll() {
    static uint32_t held_since = 0;
    static bool     started = false;
    uint32_t now = millis();

    if (boot_pending && now > STABLE_MS) {
        boot_pending = false;
        Preferences p;
        p.begin(NS, false);
        p.putBool("boot_pend", false);
        p.putUChar("boot_fail", 0);
        p.end();
    }

    if (now > WINDOW_MS) return;
    if (!started) { pinMode(0, INPUT_PULLUP); started = true; }
    if (digitalRead(0) == LOW) {
        if (!held_since) held_since = now ? now : 1;
        else if (now - held_since >= HOLD_MS) {
            Serial.println("[HW] BOOT key held: hardware configuration reset to defaults, restarting");
            hw_reset_stored();
            delay(200);
            ESP.restart();
        }
    } else {
        held_since = 0;
    }
}
