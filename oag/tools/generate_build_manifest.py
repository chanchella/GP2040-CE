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
    "phase": "U8H",
    "board": "Raspberry Pi Pico 2 W",
    "git_sha": repo_sha,
    "branch": branch,
    "features": {
        "usb_pio_host": True,
        "usb_root_ports": 3,
        "usb_hub_host": True,
        "usb_host_device_capacity": 12,
        "usb_host_hub_capacity": 4,
        "logical_gamepad_slots": 8,
        "shared_device_registry_capacity": 12,
        "host_enumeration_buffer_bytes": 1024,
        "host_root_watchdog_2s": False,
        "golden_usb_host_clock_120mhz": True,
        "golden_xusb_continuous_rearm": True,
        "xusb_input": True,
        "wired_xgip_input": True,
        "xgip_host_init_sequence": True,
        "keyboard_boot_host": True,
        "keyboard_lock_led_output": True,
        "keyboard_default_numlock_led": True,
        "g808_skip_set_idle_golden_quirk": True,
        "mouse_boot_host": True,
        "generic_hid_gamepad_host": True,
        "generic_hid_structural_fallback": True,
        "known_usb_controller_classifier": True,
        "platform_profile_registry": True,
        "platform_output_runtime_abstraction": True,
        "default_platform_profile": "PC_XINPUT_360",
        "mouse_to_stick_software_foundation": True,
        "digital_binding_engine_software_foundation": True,
        "keyboard_mouse_runtime_gamepad": True,
        "mouse_aim_recenter_us": 6000,
        "pc_generic_hid_output": False,
        "pc_xinput_output": True,
        "pc_xinput_output_slots": 4,
        "multi_xinput_output": True,
        "slot0_keyboard_mouse_overlay": True,
        "slot_scoped_rumble_reverse": True,
        "rumble_reverse_runtime": True,
        "u6e_usb_hardware_baseline_verified": True,
        "bluetooth": True,
        "bluetooth_btstack_runtime": True,
        "bluetooth_runtime_arch": "pico_cyw43_arch_none",
        "bluetooth_golden_async_context_restored": True,
        "bluetooth_btstack_timer_discovery": True,
        "bluetooth_host_only_isolation_gate": True,
        "bluetooth_golden_remote_tlv_reconnect": True,
        "bluetooth_preserve_existing_bonds": True,
        "bluetooth_first_link_btstack_limits_match_golden": True,
        "bluetooth_classic_descriptor_before_rediscovery": True,
        "bluetooth_classic_negative_pin": True,
        "bluetooth_classic_hid_host": True,
        "bluetooth_le_hog_host": True,
        "bluetooth_connection_budget": 2,
        "bluetooth_target_multi_controller_after_first_link_gate": True,
        "bluetooth_usb_settle_ms": 100,
        "bluetooth_continuous_discovery": True,
        "bluetooth_active_le_scan": True,
        "bluetooth_hid_uuid_or_appearance_or_name_detection": True,
        "bluetooth_diagnostic_led": True,
        "bluetooth_stage_coded_led_diagnostic": True,
        "bluetooth_xinput_slot3_diagnostic_overlay": True,
        "bluetooth_xinput_diagnostic_mapping": {
            "A": "stage 1 - HCI working/searching",
            "B": "stage 2 - controller candidate detected",
            "X": "stage 3 - connect request accepted",
            "Y": "stage 4 - transport link opened",
            "LB": "stage 5 - BLE security succeeded",
            "RB": "stage 6 - HID ready",
            "Start": "failure at current stage",
            "Guide_plus_ABXYLBRB": "live HID report received"
        },
        "bluetooth_diagnostic_stage_meanings": {
            "1": "HCI working / searching",
            "2": "controller candidate detected",
            "3": "connect request accepted",
            "4": "transport link opened",
            "5": "BLE security pairing succeeded",
            "6": "HID service or descriptor ready",
            "solid": "input report received",
            "long_after_stage": "failure at current stage"
        },
        "bluetooth_generic_hid_gamepad_to_logical_slot": True,
        "bluetooth_keyboard_mouse_runtime": False,
        "bluetooth_output_peripheral_foundation": True,
        "bluetooth_output_peripheral_runtime": False,
        "bluetooth_ble_hid_gamepad_advertising": False,
        "bluetooth_ble_hid_gamepad_slot0_runtime": False,
        "touchscreen_output_foundation": True,
        "touchscreen_usb_runtime": False,
        "drawing_tablet_output_foundation": True,
        "drawing_tablet_usb_runtime": False,
        "universal_output_modes_foundation": [
            "GAMEPAD",
            "TOUCHSCREEN",
            "DRAWING_TABLET",
            "MOUSE_KEYBOARD",
            "HYBRID",
        ],
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
