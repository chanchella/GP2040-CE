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

# Golden G808 HID-host quirk. The 2563:0575 receiver can leave its usable
# controller mode when TinyUSB sends SET_IDLE during enumeration. Apply only
# to the exact pinned TinyUSB source and fail closed if the source changes.
hid_host = ROOT / "lib/tinyusb/src/class/hid/hid_host.c"
hid_text = hid_host.read_text(encoding="utf-8")

idle_source = """    case CONFG_SET_IDLE: {
      // Idle rate = 0 mean only report when there is changes
      const uint16_t idle_rate = 0;
      const uintptr_t next_state = (p_hid->itf_protocol != HID_ITF_PROTOCOL_NONE)
                                   ? CONFIG_SET_PROTOCOL : CONFIG_GET_REPORT_DESC;
      _hidh_set_idle(daddr, itf_num, idle_rate, process_set_config, next_state);
      break;
    }
"""

idle_patched = """    case CONFG_SET_IDLE: {
      // Idle rate = 0 mean only report when there is changes
      const uint16_t idle_rate = 0;
      const uintptr_t next_state = (p_hid->itf_protocol != HID_ITF_PROTOCOL_NONE)
                                   ? CONFIG_SET_PROTOCOL : CONFIG_GET_REPORT_DESC;

      // OAG Golden quirk: Redragon G808 receiver 2563:0575 can leave its
      // usable controller mode when SET_IDLE is issued. Skip only for this
      // exact identity and continue TinyUSB's normal config state machine.
      uint16_t vid = 0;
      uint16_t pid = 0;
      const bool skip_set_idle =
          tuh_vid_pid_get(daddr, &vid, &pid) &&
          vid == 0x2563 && pid == 0x0575;

      if (skip_set_idle) {
        xfer->user_data = next_state;
        process_set_config(xfer);
      } else {
        _hidh_set_idle(daddr, itf_num, idle_rate, process_set_config, next_state);
      }
      break;
    }
"""

if idle_patched in hid_text:
    pass
elif idle_source in hid_text:
    hid_host.write_text(
        hid_text.replace(idle_source, idle_patched, 1),
        encoding="utf-8",
    )
else:
    raise SystemExit(
        "TinyUSB G808 SET_IDLE precondition failed; refusing to patch"
    )

hid_verify = hid_host.read_text(encoding="utf-8")
if idle_patched not in hid_verify:
    raise SystemExit("TinyUSB G808 SET_IDLE patch verification failed")

patch_record = "\n".join(
    [
        EXPECTED[ROOT / "lib/pico_pio_usb"],
        EXPECTED[ROOT / "lib/tinyusb"],
        patched,
        "G808_SKIP_SET_IDLE_2563_0575",
    ]
)

print("OAG_DEPENDENCY_PREPARE=PASS")
print(f"PICO_PIO_USB_SHA={EXPECTED[ROOT / 'lib/pico_pio_usb']}")
print(f"TINYUSB_SHA={EXPECTED[ROOT / 'lib/tinyusb']}")
print("PIO_USB_ROOT_PORT_CNT=3")
print("PIO_USB_RP2350_E9_WORKAROUND=UPSTREAM")
print("PIO_USB_RUNTIME_PATCHES=NONE")
print("TINYUSB_G808_SKIP_SET_IDLE=YES")
print(f"OAG_PIO_PATCH_DIGEST={sha256_text(patch_record)}")
