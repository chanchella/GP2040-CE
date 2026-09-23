#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit(
        "usage: generate_build_manifest.py <build-dir> <output-json>"
    )

build_dir = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
root = Path(__file__).resolve().parents[2]

uf2_files = sorted(build_dir.glob("*.uf2"))
elf_files = sorted(build_dir.glob("*.elf"))

if len(uf2_files) != 1:
    raise SystemExit(
        f"expected exactly one UF2 in {build_dir}, found {len(uf2_files)}"
    )

if len(elf_files) != 1:
    raise SystemExit(
        f"expected exactly one ELF in {build_dir}, found {len(elf_files)}"
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), *args],
        text=True,
    ).strip()


manifest = {
    "product": "OAG Abo Gemi Ultra Gaming",
    "firmware_family": "OAG Universal Input Dongle",
    "phase": "U1-HW1",
    "board": "Raspberry Pi Pico 2 W",
    "git_sha": git("rev-parse", "HEAD"),
    "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
    "features": {
        "usb_pio_host": True,
        "usb_root_ports": 3,
        "xusb_input": True,
        "pc_generic_hid_output": True,
        "bluetooth": False,
        "keyboard": False,
        "mouse": False,
        "rumble": False,
        "authentication": False,
    },
    "dependencies": {
        "pico_pio_usb": "37965f8895fffb4ffacace86e1f731f908dc18e0",
        "tinyusb": "9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d",
    },
    "artifacts": {
        "uf2": {
            "name": uf2_files[0].name,
            "size": uf2_files[0].stat().st_size,
            "sha256": sha256(uf2_files[0]),
        },
        "elf": {
            "name": elf_files[0].name,
            "size": elf_files[0].stat().st_size,
            "sha256": sha256(elf_files[0]),
        },
    },
    "verification": {
        "source_exists": True,
        "software_verified": True,
        "ci_verified": False,
        "hardware_verified": False,
    },
}

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(
    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(f"OAG_BUILD_MANIFEST={output}")
