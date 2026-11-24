#include "hw.h"
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    HwConfig c;
    hw_defaults(c);
    CHECK(hw_validate(c) == nullptr);                 // the author's board must validate
    for (int p = 0; p < PANEL_COUNT; p++) { HwConfig d = c; hw_preset(d, p); CHECK(hw_validate(d) == nullptr); }
    HwConfig d;
    d = c; d.mosi = 6;  CHECK(hw_validate(d));        // flash pin
    d = c; d.mosi = 1;  CHECK(hw_validate(d));        // UART0
    d = c; d.mosi = 0;  CHECK(hw_validate(d));        // BOOT
    d = c; d.mosi = 34; CHECK(hw_validate(d));        // input only as output
    d = c; d.miso = 34; CHECK(hw_validate(d) == nullptr);   // input only is fine for MISO
    d = c; d.touch_irq = 35; CHECK(hw_validate(d) == nullptr);
    d = c; d.touch_irq = 27; CHECK(hw_validate(d) == nullptr);
    d = c; d.dc = 15;   CHECK(hw_validate(d));        // duplicate with CS
    d = c; d.touch_cs = 15; CHECK(hw_validate(d));
    d = c; d.rst = -1;  CHECK(hw_validate(d) == nullptr);
    d = c; d.bl = -1;   CHECK(hw_validate(d) == nullptr);
    d = c; d.cs = -1;   CHECK(hw_validate(d));
    d = c; d.write_mhz = 0;  CHECK(hw_validate(d));
    d = c; d.write_mhz = 41; CHECK(hw_validate(d));
    d = c; d.read_mhz = 21;  CHECK(hw_validate(d));
    d = c; d.panel = 9;      CHECK(hw_validate(d));
    d = c; d.touch_on = 0; d.touch_cs = -1; CHECK(hw_validate(d) == nullptr);
    d = c; d.mosi = 24;  CHECK(hw_validate(d));       // does not exist
    d = c; d.mosi = 40;  CHECK(hw_validate(d));
    // buzzer pin: optional, an output pin, no duplicates
    CHECK(c.buzzer == 26);                              // the author's board
    d = c; d.buzzer = -1;  CHECK(hw_validate(d) == nullptr);
    d = c; d.buzzer = 27;  CHECK(hw_validate(d) == nullptr);
    d = c; d.buzzer = 34;  CHECK(hw_validate(d));       // input only
    d = c; d.buzzer = 6;   CHECK(hw_validate(d));       // flash
    d = c; d.buzzer = 0;   CHECK(hw_validate(d));       // BOOT
    d = c; d.buzzer = 32;  CHECK(hw_validate(d));       // backlight
    d = c; d.buzzer = 14;  CHECK(hw_validate(d));       // touch CS
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
