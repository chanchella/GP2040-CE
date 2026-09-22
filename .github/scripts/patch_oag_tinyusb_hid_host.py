#!/usr/bin/env python3
from pathlib import Path

path = Path("lib/tinyusb/src/class/hid/hid_host.c")
text = path.read_text()

old = """bool hidh_set_config(uint8_t daddr, uint8_t itf_num) {
  tusb_control_request_t request;
  request.wIndex = tu_htole16((uint16_t) itf_num);

  tuh_xfer_t xfer;
  xfer.daddr = daddr;
  xfer.result = XFER_RESULT_SUCCESS;
  xfer.setup = &request;
  xfer.user_data = CONFG_SET_IDLE;

  // fake request to kick-off the set config process
  process_set_config(&xfer);

  return true;
}
"""

new = """bool hidh_set_config(uint8_t daddr, uint8_t itf_num) {
  tusb_control_request_t request;
  request.bRequest = HID_REQ_CONTROL_SET_IDLE;
  request.wIndex = tu_htole16((uint16_t) itf_num);

  // OAG Vortex: some Shanwan/Redragon-style 2.4 GHz receivers are known
  // to re-enumerate into a reduced fallback mode when hosts issue SET_IDLE
  // during initial HID enumeration. Skip only that optional request for the
  // researched receiver identities; all normal HID devices retain upstream
  // TinyUSB behaviour.
  uint16_t vid = 0;
  uint16_t pid = 0;
  bool skip_set_idle = false;

  if (tuh_vid_pid_get(daddr, &vid, &pid)) {
    skip_set_idle =
        (vid == 0x2563 && pid == 0x0575);
  }

  tuh_xfer_t xfer;
  xfer.daddr = daddr;
  xfer.result = XFER_RESULT_SUCCESS;
  xfer.setup = &request;
  xfer.user_data = skip_set_idle ? CONFIG_GET_REPORT_DESC : CONFG_SET_IDLE;

  // fake request to kick-off the set config process
  process_set_config(&xfer);

  return true;
}
"""

if old not in text:
    raise SystemExit("Expected TinyUSB hidh_set_config block not found")

text = text.replace(old, new, 1)
path.write_text(text)

if "skip_set_idle" not in text or "0x2563" not in text:
    raise SystemExit("OAG TinyUSB HID patch verification failed")

print("OAG_TINYUSB_HID_QUIRK_PATCH=PASS")
