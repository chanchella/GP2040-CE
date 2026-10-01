#!/usr/bin/env python3
"""Compile the actual hardware-dependent shortcut handlers in a host harness."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
signatures = (
    "    std::uint16_t oagDigitMask(const oag::KeyboardState& keyboard) const {",
    "    void cancelGameProfile() {",
    "    void serviceOagGameWeaponHotkey() {",
    "    void maskOagGameWeaponHotkey(oag::KeyboardState& keyboard) const {",
)
functions = []
for signature in signatures:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if end >= len(source):
            raise SystemExit("Unterminated profile handler: " + signature)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[start:end])

Path(sys.argv[2]).write_text(
    "// Generated from firmware/src/main.cpp; do not edit.\n" +
    "\n".join(functions) + "\n", encoding="utf-8"
)
