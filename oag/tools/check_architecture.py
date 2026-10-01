#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

RULES = {
    "include/oag/device": ["tusb.h", "pio_usb.h", "btstack"],
    "include/oag/input": ["tusb.h", "pio_usb.h", "btstack"],
    "include/oag/mapping": ["tusb.h", "pio_usb.h", "btstack"],
    "src/mapping": ["tusb.h", "pio_usb.h", "btstack"],
}

errors = []

for relative, forbidden in RULES.items():
    base = ROOT / relative
    if not base.exists():
        continue

    for path in base.rglob("*"):
        if path.suffix not in {".h", ".hpp", ".c", ".cc", ".cpp"}:
            continue

        source = path.read_text(encoding="utf-8")
        for token in forbidden:
            if token in source:
                errors.append(
                    f"{path.relative_to(ROOT)}: forbidden dependency '{token}'"
                )

product_file = ROOT / "include/oag/core/product_identity.h"
product_text = product_file.read_text(encoding="utf-8")
if "OAG Abo Gemi Ultra Gaming" not in product_text:
    errors.append("product identity does not contain required product name")

# F1+0 is a game-context command. Guard the hardware boundary too: unit
# tests cover mapped input and generated holds; this prevents the shortcut
# itself from switching descriptors, rebooting, disabling KM or saving Flash.
firmware_text = (ROOT / "firmware/src/main.cpp").read_text(encoding="utf-8")
cancel = re.search(r"void cancelGameProfile\(\) \{(.*?)\n    \}", firmware_text, re.S)
if cancel is None:
    errors.append("F1+0 cancellation handler is missing")
else:
    body = re.sub(r"//[^\n]*", "", cancel[1])
    for forbidden in ("requestOutputProfile", "watchdog", "keyboardMouseMode_", "nativeKmOutput_",
                      "proInput_", "currentMouseMotion_", "pubgMovement", "registry_",
                      "bluetoothHost_", "usbHost_", ".save("):
        if forbidden in body:
            errors.append(f"F1+0 must preserve physical input/transport: {forbidden}")
composed = firmware_text.split("void sendComposedOutput() {", 1)[1].split("void updatePubgTriangleState(", 1)[0]
if composed.count("!gameContextInactive_") != 3 or "if (gameContextInactive_)" in composed:
    errors.append("All three physical output routes must precede the game-effects gate")
toggle = firmware_text.split("void serviceKeyboardMouseModeToggle() {", 1)[1].split("void serviceNativeKeyboardMouseOutput(", 1)[0]
if "gameContextInactive_" in toggle:
    errors.append("F1+0 must leave the Native/Controller mode toggle available")

if errors:
    print("OAG_ARCHITECTURE_CHECK=FAIL")
    for error in errors:
        print(error)
    sys.exit(1)

print("OAG_ARCHITECTURE_CHECK=PASS")
