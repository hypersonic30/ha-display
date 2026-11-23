# PlatformIO pre-script: bake the firmware version and the display variant into the build.
# The version lives in firmware/VERSION (it only changes when the firmware changes, not with every
# integration release); the variant is the PlatformIO environment name (ili9488, ili9481, ...).
Import("env")
import os

path = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")
if not os.path.isfile(path):
    raise SystemExit("firmware/VERSION is missing")
version = open(path).read().strip()
if not version:
    raise SystemExit("firmware/VERSION is empty")

env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(version)),
    ("FW_VARIANT", env.StringifyMacro(env["PIOENV"])),
])
