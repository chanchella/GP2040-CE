#!/usr/bin/env python3
from pathlib import Path
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

if errors:
    print("OAG_ARCHITECTURE_CHECK=FAIL")
    for error in errors:
        print(error)
    sys.exit(1)

print("OAG_ARCHITECTURE_CHECK=PASS")
