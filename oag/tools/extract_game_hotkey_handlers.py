#!/usr/bin/env python3
"""Compile the production hotkey bodies against host transport/storage spies."""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
handlers = []
for name in ("cancelGameProfile", "serviceOagGameWeaponHotkey"):
    match = re.search(
        rf"^    void {name}\(\) \{{.*?^    \}}",
        source,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise SystemExit(f"Production hotkey handler missing: {name}")
    handlers.append(match[0])
Path(sys.argv[2]).write_text("\n\n".join(handlers) + "\n", encoding="utf-8")
