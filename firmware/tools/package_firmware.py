"""Collect the built images into the release layout. Run after `pio run`, before committing a firmware change.

  python tools/package_firmware.py

Writes custom_components/display/firmware/ (what the integration serves over the air, with manifest.json) and
firmware/bin/ (what you flash by cable with esptool). Refuses stale builds: the version and the variant name have
to be inside the binary.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware"
VARIANTS = ("ili9488", "ili9481")


def main() -> int:
    version = (FW / "VERSION").read_text().strip()
    bundle = ROOT / "custom_components" / "display" / "firmware"
    bundle.mkdir(exist_ok=True)
    (FW / "bin").mkdir(exist_ok=True)
    manifest = {"version": version, "variants": {}}
    for name in VARIANTS:
        built = FW / ".pio" / "build" / name / "firmware.bin"
        if not built.is_file():
            print(f"missing {built} - run `pio run` first")
            return 1
        data = built.read_bytes()
        for needle in (version.encode(), name.encode()):
            if needle not in data:
                print(f"{built} does not contain {needle!r}: stale build? run `pio run` again")
                return 1
        target = f"firmware-{name}.bin"
        (bundle / target).write_bytes(data)
        shutil.copyfile(built, FW / "bin" / target)
        manifest["variants"][name] = {
            "file": target,
            "size": len(data),
            "md5": hashlib.md5(data).hexdigest(),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    for extra in ("bootloader.bin", "partitions.bin"):
        shutil.copyfile(FW / ".pio" / "build" / VARIANTS[0] / extra, FW / "bin" / extra)
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"packaged firmware {version}: " + ", ".join(f"{n} {v['size']} bytes" for n, v in manifest["variants"].items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
