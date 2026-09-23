#!/usr/bin/env python3
from pathlib import Path
import hashlib
import subprocess

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

# ------------------------------------------------------------------
# Patch 1: exactly three PIO USB roots for P1/P2/P3.
# ------------------------------------------------------------------
cfg = ROOT / "lib/pico_pio_usb/src/pio_usb_configuration.h"
cfg_text = cfg.read_text(encoding="utf-8")

root_source = "#define PIO_USB_ROOT_PORT_CNT 2"
root_patched = (
    "#define PIO_USB_ROOT_PORT_CNT 3 "
    "// OAG U1 deterministic three-root build"
)

if root_patched in cfg_text:
    pass
elif root_source in cfg_text:
    cfg_text = cfg_text.replace(root_source, root_patched, 1)
    cfg.write_text(cfg_text, encoding="utf-8")
else:
    raise SystemExit(
        "Pico-PIO-USB root-port precondition failed; refusing to patch"
    )

if root_patched not in cfg.read_text(encoding="utf-8"):
    raise SystemExit("Three-root dependency verification failed")

# ------------------------------------------------------------------
# Patch 2: full endpoint reset on device close.
#
# The pinned Pico-PIO-USB close path only cleared size/has_transfer.
# A hot-unplug therefore could leave endpoint lifecycle state behind
# (new_data_flag, transfer_aborted, transfer_started, counters, etc.).
# A power-cycle clears the whole pool, which matches the observed
# recovery behavior. OAG intentionally makes close deterministic by
# resetting the complete endpoint object before it can be reused.
# ------------------------------------------------------------------
host = ROOT / "lib/pico_pio_usb/src/pio_usb_host.c"
host_text = host.read_text(encoding="utf-8")

close_source = """    if ((ep->root_idx == root_idx) && (ep->dev_addr == device_address) &&
        ep->size) {
      ep->size = 0;
      ep->has_transfer = false;
    }"""

close_patched = """    if ((ep->root_idx == root_idx) && (ep->dev_addr == device_address) &&
        ep->size) {
      memset(ep, 0, sizeof(*ep));
    }"""

if close_patched in host_text:
    pass
elif close_source in host_text:
    host_text = host_text.replace(close_source, close_patched, 1)
    host.write_text(host_text, encoding="utf-8")
else:
    raise SystemExit(
        "Pico-PIO-USB close-device precondition failed; refusing to patch"
    )

verify_host = host.read_text(encoding="utf-8")
if close_patched not in verify_host:
    raise SystemExit("Endpoint close-state reset verification failed")

patch_record = (
    root_patched
    + "\n"
    + close_patched
    + "\n"
    + EXPECTED[ROOT / "lib/pico_pio_usb"]
)

print("OAG_DEPENDENCY_PREPARE=PASS")
print(f"PICO_PIO_USB_SHA={EXPECTED[ROOT / 'lib/pico_pio_usb']}")
print(f"TINYUSB_SHA={EXPECTED[ROOT / 'lib/tinyusb']}")
print("PIO_USB_ROOT_PORT_CNT=3")
print("PIO_USB_ENDPOINT_CLOSE_RESET=FULL")
print(f"OAG_PIO_PATCH_DIGEST={sha256_text(patch_record)}")
