#include "hw_json.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    HwConfig base;
    hw_defaults(base);

    // only the keys that are present change
    JsonDocument doc;
    deserializeJson(doc, "{\"write_mhz\":5,\"touch_irq\":27,\"rst\":-1}");
    HwConfig c = base;
    hw_apply_json(c, doc.as<JsonObjectConst>());
    CHECK(c.write_mhz == 5 && c.touch_irq == 27 && c.rst == -1);
    CHECK(c.mosi == base.mosi && c.read_mhz == base.read_mhz && c.buzzer == base.buzzer && c.panel == base.panel);
    CHECK(!hw_equal(c, base));
    CHECK(hw_validate(c) == nullptr);

    // an empty object changes nothing
    JsonDocument empty;
    deserializeJson(empty, "{}");
    HwConfig d = base;
    hw_apply_json(d, empty.as<JsonObjectConst>());
    CHECK(hw_equal(d, base));

    // the version byte does not count for equality (a stored v1 vs a v2 struct)
    d.ver = 1;
    CHECK(hw_equal(d, base));

    // a full round trip through JSON keeps every field
    JsonDocument rt;
    hw_to_json(c, rt.to<JsonObject>());
    HwConfig e = base;
    hw_apply_json(e, rt.as<JsonObjectConst>());
    CHECK(hw_equal(e, c));

    // a bad set from the server is caught by the validator (the firmware ignores it)
    JsonDocument bad;
    deserializeJson(bad, "{\"mosi\":6}");
    HwConfig f = base;
    hw_apply_json(f, bad.as<JsonObjectConst>());
    CHECK(hw_validate(f) != nullptr);

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("all ok\n");
    return 0;
}
