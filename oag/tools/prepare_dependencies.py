#!/usr/bin/env python3
from pathlib import Path
import hashlib
import subprocess

ROOT = Path(__file__).resolve().parents[2]

EXPECTED = {
    ROOT / "lib/pico_pio_usb": "5a37a66dc5d3fbe0ef3cdbeda923a757440f984f",
    ROOT / "lib/tinyusb": "9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d",
}


def git_head(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        text=True,
    ).strip()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


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
patched = (
    "#define PIO_USB_ROOT_PORT_CNT 3 "
    "// OAG three-root hardware"
)

if patched in text:
    pass
elif source in text:
    cfg.write_text(text.replace(source, patched, 1), encoding="utf-8")
else:
    raise SystemExit(
        "Pico-PIO-USB root-count precondition failed; refusing to patch"
    )

verify = cfg.read_text(encoding="utf-8")
if patched not in verify:
    raise SystemExit("Three-root dependency verification failed")

patch_record = "\n".join(
    [
        EXPECTED[ROOT / "lib/pico_pio_usb"],
        EXPECTED[ROOT / "lib/tinyusb"],
        patched,
    ]
)

print("OAG_DEPENDENCY_PREPARE=PASS")
print(f"PICO_PIO_USB_SHA={EXPECTED[ROOT / 'lib/pico_pio_usb']}")
print(f"TINYUSB_SHA={EXPECTED[ROOT / 'lib/tinyusb']}")
print("PIO_USB_ROOT_PORT_CNT=3")
print("PIO_USB_RP2350_E9_WORKAROUND=UPSTREAM")
print("PIO_USB_RUNTIME_PATCHES=NONE")
print(f"OAG_PIO_PATCH_DIGEST={sha256_text(patch_record)}")
