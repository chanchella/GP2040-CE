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


def git_in(path: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), *args],
        text=True,
    ).strip()


repo_sha = git_in(root, "rev-parse", "HEAD")
branch = git_in(root, "rev-parse", "--abbrev-ref", "HEAD")
pio_sha = git_in(root / "lib/pico_pio_usb", "rev-parse", "HEAD")
tinyusb_sha = git_in(root / "lib/tinyusb", "rev-parse", "HEAD")

manifest = {
    "product": "OAG Abo Gemi Ultra Gaming",
    "firmware_family": "OAG Universal Input Dongle",
    "phase": "U2E",
    "board": "Raspberry Pi Pico 2 W",
    "git_sha": repo_sha,
    "branch": branch,
    "features": {
        "usb_pio_host": True,
        "usb_root_ports": 3,
        "host_root_watchdog_2s": True,
        "xusb_input": True,
        "keyboard_boot_host": True,
        "mouse_boot_host": True,
        "mouse_to_stick_software_foundation": True,
        "digital_binding_engine_software_foundation": True,
        "pc_generic_hid_output": False,
        "pc_xinput_output": True,
        "rumble_reverse_runtime": True,
        "bluetooth": False,
        "authentication": False,
    },
    "pc_xinput_profile": {
        "compatibility_vid": "0x045E",
        "compatibility_pid": "0x028E",
        "oag_strings": True,
        "pico_unique_serial": True,
        "console_authentication": False,
    },
    "dependencies": {
        "pico_pio_usb_repo": "sekigon-gonnoc/Pico-PIO-USB",
        "pico_pio_usb": pio_sha,
        "tinyusb_repo": "OpenStickCommunity/tinyusb",
        "tinyusb": tinyusb_sha,
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
        "ci_verified": True,
        "hardware_verified": False,
    },
}

expected_pio = "5a37a66dc5d3fbe0ef3cdbeda923a757440f984f"
expected_tinyusb = "9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d"

if pio_sha != expected_pio:
    raise SystemExit(
        f"manifest dependency mismatch: pico_pio_usb {pio_sha} != {expected_pio}"
    )

if tinyusb_sha != expected_tinyusb:
    raise SystemExit(
        f"manifest dependency mismatch: tinyusb {tinyusb_sha} != {expected_tinyusb}"
    )

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(
    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(f"OAG_BUILD_MANIFEST={output}")
print(f"OAG_MANIFEST_PIO_SHA={pio_sha}")
print(f"OAG_MANIFEST_TINYUSB_SHA={tinyusb_sha}")
