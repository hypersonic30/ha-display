#pragma once
#include <ArduinoJson.h>
#include "hw.h"

// Overlay the keys present in `o` onto `c` (missing keys keep their value). Used by the portal and by the
// `settings.hw` object of the layout frame, so both speak the same keys.
void hw_apply_json(HwConfig &c, JsonObjectConst o);
void hw_to_json(const HwConfig &c, JsonObject o);
