#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

EXPECTED = {
    ROOT / "lib/pico_pio_usb": "37965f8895fffb4ffacace86e1f731f908dc18e0",
    ROOT / "lib/tinyusb": "9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d",
}


def git_head(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        text=True,
    ).strip()


for path, expected in EXPECTED.items():
    if not path.exists():
        raise SystemExit(f"Missing dependency: {path}")

    actual = git_head(path)
    if actual != expected:
        raise SystemExit(
            f"Dependency SHA mismatch for {path.name}: "
            f"expected {expected}, got {actual}"
        )

cfg = ROOT / "lib/pico_pio_usb/src/pio_usb_configuration.h"
text = cfg.read_text(encoding="utf-8")

source = "#define PIO_USB_ROOT_PORT_CNT 2"
patched = "#define PIO_USB_ROOT_PORT_CNT 3 // OAG U1 deterministic three-root build"

if patched in text:
    pass
elif source in text:
    text = text.replace(source, patched, 1)
    cfg.write_text(text, encoding="utf-8")
else:
    raise SystemExit(
        "Pico-PIO-USB root-port precondition failed; refusing to patch"
    )

verify = cfg.read_text(encoding="utf-8")
if patched not in verify:
    raise SystemExit("Three-root dependency verification failed")

print("OAG_DEPENDENCY_PREPARE=PASS")
print(f"PICO_PIO_USB_SHA={EXPECTED[ROOT / 'lib/pico_pio_usb']}")
print(f"TINYUSB_SHA={EXPECTED[ROOT / 'lib/tinyusb']}")
print("PIO_USB_ROOT_PORT_CNT=3")
