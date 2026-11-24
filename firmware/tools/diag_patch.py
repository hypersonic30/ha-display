# PlatformIO pre-script, diagnostic environments only: choose the ILI9481 init sequence of TFT_eSPI
# (it is a hard-coded #define in TFT_Drivers/ILI9481_Init.h) via `custom_diag_init = N` in platformio.ini.
# Patches only this environment's own copy of the library; the production environments never load this script.
Import("env")
import glob, os, re

n = env.GetProjectOption("custom_diag_init", "")
if n:
    root = env.subst("$PROJECT_LIBDEPS_DIR")
    for path in glob.glob(os.path.join(root, env.subst("$PIOENV"), "TFT_eSPI", "TFT_Drivers", "ILI9481_Init.h")):
        text = open(path).read()
        new = re.sub(r"(?m)^\s*(//)?\s*#define ILI9481_INIT_(\d)\b",
                     lambda m: ("#define" if m.group(2) == n else "//#define") + " ILI9481_INIT_" + m.group(2), text)
        if new != text:
            open(path, "w").write(new)
