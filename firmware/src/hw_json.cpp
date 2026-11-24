#include "hw_json.h"

void hw_apply_json(HwConfig &c, JsonObjectConst o) {
    c.panel     = o["panel"] | c.panel;
    c.mosi      = o["mosi"] | c.mosi;   c.sclk = o["sclk"] | c.sclk;   c.miso = o["miso"] | c.miso;
    c.cs        = o["cs"] | c.cs;       c.dc   = o["dc"] | c.dc;       c.rst  = o["rst"] | c.rst;
    c.bl        = o["bl"] | c.bl;
    c.bl_invert = o["bl_invert"] | c.bl_invert;
    c.write_mhz = o["write_mhz"] | c.write_mhz;   c.read_mhz = o["read_mhz"] | c.read_mhz;
    c.touch_on  = o["touch_on"] | c.touch_on;
    c.touch_cs  = o["touch_cs"] | c.touch_cs;     c.touch_irq = o["touch_irq"] | c.touch_irq;
    c.bgr       = o["bgr"] | c.bgr;               c.invert = o["invert"] | c.invert;
    c.buzzer    = o["buzzer"] | c.buzzer;
}

void hw_to_json(const HwConfig &c, JsonObject o) {
    o["panel"] = c.panel;
    o["mosi"] = c.mosi; o["sclk"] = c.sclk; o["miso"] = c.miso;
    o["cs"] = c.cs; o["dc"] = c.dc; o["rst"] = c.rst; o["bl"] = c.bl;
    o["bl_invert"] = c.bl_invert; o["write_mhz"] = c.write_mhz; o["read_mhz"] = c.read_mhz;
    o["touch_on"] = c.touch_on; o["touch_cs"] = c.touch_cs; o["touch_irq"] = c.touch_irq;
    o["bgr"] = c.bgr; o["invert"] = c.invert; o["buzzer"] = c.buzzer;
}
