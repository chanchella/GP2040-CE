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


def replace_once(path: Path, source: str, patched: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if patched in text:
        return
    if source not in text:
        raise SystemExit(f"{label} precondition failed; refusing to patch")
    path.write_text(text.replace(source, patched, 1), encoding="utf-8")


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
host = ROOT / "lib/pico_pio_usb/src/pio_usb_host.c"
core = ROOT / "lib/pico_pio_usb/src/pio_usb.c"

# Patch 1: exactly three PIO USB roots for P1/P2/P3.
root_source = "#define PIO_USB_ROOT_PORT_CNT 2"
root_patched = (
    "#define PIO_USB_ROOT_PORT_CNT 3 "
    "// OAG U1 deterministic three-root build"
)
replace_once(cfg, root_source, root_patched, "root-port count")

# Patch 2: full endpoint reset when TinyUSB closes a device.
close_source = """    if ((ep->root_idx == root_idx) && (ep->dev_addr == device_address) &&
        ep->size) {
      ep->size = 0;
      ep->has_transfer = false;
    }"""
close_patched = """    if ((ep->root_idx == root_idx) && (ep->dev_addr == device_address) &&
        ep->size) {
      memset(ep, 0, sizeof(*ep));
    }"""
replace_once(host, close_source, close_patched, "close-device endpoint reset")

# Patch 3: hot-unplug is represented as one root removal event.
#
# The pinned implementation first retires every endpoint as FAILED and then
# emits DISCONNECT. With multiple roots this creates a burst of stale endpoint
# events before TinyUSB processes DEVICE_REMOVE. OAG clears this root's endpoint
# pool immediately and emits only the root removal event.
disconnect_source = """      // device disconnect
      port->connected = false;
      port->suspended = true;
      port->ints |= PIO_USB_INTS_DISCONNECT_BITS;

      // failed/retired all queuing transfer in this root
      uint8_t root_idx = port - PIO_USB_ROOT_PORT(0);
      for (int ep_idx = 0; ep_idx < PIO_USB_EP_POOL_CNT; ep_idx++) {
        endpoint_t *ep = PIO_USB_ENDPOINT(ep_idx);
        if ((ep->root_idx == root_idx) && ep->size && ep->has_transfer) {
          pio_usb_ll_transfer_complete(ep, PIO_USB_INTS_ENDPOINT_ERROR_BITS);
        }
      }

      return false;"""

disconnect_patched = """      // OAG multi-root hot-unplug: retire this root atomically.
      uint8_t root_idx = port - PIO_USB_ROOT_PORT(0);

      port->connected = false;
      port->suspended = true;
      port->addr0_exists = false;
      port->root_device = NULL;

      for (int ep_idx = 0; ep_idx < PIO_USB_EP_POOL_CNT; ep_idx++) {
        endpoint_t *ep = PIO_USB_ENDPOINT(ep_idx);
        if ((ep->root_idx == root_idx) && ep->size) {
          memset(ep, 0, sizeof(*ep));
        }
      }

      port->ep_complete = 0;
      port->ep_error = 0;
      port->ep_stalled = 0;
      port->ints &= ~(PIO_USB_INTS_ENDPOINT_COMPLETE_BITS |
                      PIO_USB_INTS_ENDPOINT_ERROR_BITS |
                      PIO_USB_INTS_ENDPOINT_STALLED_BITS |
                      PIO_USB_INTS_CONNECT_BITS);
      port->ints |= PIO_USB_INTS_DISCONNECT_BITS;

      return false;"""

replace_once(host, disconnect_source, disconnect_patched, "root disconnect")

# Patch 4: re-arm a disconnected root from a clean runtime state before
# TinyUSB starts the next enumeration.
connect_source = """      if (line_state == PORT_PIN_FS_IDLE || line_state == PORT_PIN_LS_IDLE) {
        root->is_fullspeed = (line_state == PORT_PIN_FS_IDLE);
        root->connected = true;
        root->suspended = true; // need a bus reset before operating
        root->ints |= PIO_USB_INTS_CONNECT_BITS;
      }"""

connect_patched = """      if (line_state == PORT_PIN_FS_IDLE || line_state == PORT_PIN_LS_IDLE) {
        root->is_fullspeed = (line_state == PORT_PIN_FS_IDLE);
        root->addr0_exists = false;
        root->root_device = NULL;
        root->ep_complete = 0;
        root->ep_error = 0;
        root->ep_stalled = 0;
        root->ints &= ~(PIO_USB_INTS_DISCONNECT_BITS |
                        PIO_USB_INTS_ENDPOINT_COMPLETE_BITS |
                        PIO_USB_INTS_ENDPOINT_ERROR_BITS |
                        PIO_USB_INTS_ENDPOINT_STALLED_BITS |
                        PIO_USB_INTS_CONNECT_BITS);
        root->connected = true;
        root->suspended = true; // TinyUSB will issue the required bus reset
        root->ints |= PIO_USB_INTS_CONNECT_BITS;
      }"""

replace_once(host, connect_source, connect_patched, "root reconnect re-arm")

# Patch 5: additional roots are explicitly host roots, matching root 0.
add_port_source = """      port_pin_drive_setting(root);
      root->initialized = true;

      return 0;"""

add_port_patched = """      port_pin_drive_setting(root);
      root->mode = PIO_USB_MODE_HOST;
      root->event = EVENT_NONE;
      root->addr0_exists = false;
      root->root_device = NULL;
      root->connected = false;
      root->suspended = false;
      root->ints = 0;
      root->ep_complete = 0;
      root->ep_error = 0;
      root->ep_stalled = 0;
      root->initialized = true;

      return 0;"""

replace_once(core, add_port_source, add_port_patched, "additional root initialization")

verifications = {
    "PIO_USB_ROOT_PORT_CNT=3": root_patched in cfg.read_text(encoding="utf-8"),
    "PIO_USB_ENDPOINT_CLOSE_RESET=FULL": close_patched in host.read_text(encoding="utf-8"),
    "PIO_USB_DISCONNECT=ATOMIC_ROOT_REMOVE": disconnect_patched in host.read_text(encoding="utf-8"),
    "PIO_USB_RECONNECT=CLEAN_ROOT_REARM": connect_patched in host.read_text(encoding="utf-8"),
    "PIO_USB_ADDITIONAL_ROOTS=HOST_MODE": add_port_patched in core.read_text(encoding="utf-8"),
}

for label, ok in verifications.items():
    if not ok:
        raise SystemExit(f"{label} verification failed")

patch_record = "\n".join(
    [
        EXPECTED[ROOT / "lib/pico_pio_usb"],
        root_patched,
        close_patched,
        disconnect_patched,
        connect_patched,
        add_port_patched,
    ]
)

print("OAG_DEPENDENCY_PREPARE=PASS")
print(f"PICO_PIO_USB_SHA={EXPECTED[ROOT / 'lib/pico_pio_usb']}")
print(f"TINYUSB_SHA={EXPECTED[ROOT / 'lib/tinyusb']}")
for label in verifications:
    print(label)
print(f"OAG_PIO_PATCH_DIGEST={sha256_text(patch_record)}")
