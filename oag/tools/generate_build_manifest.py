#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import os
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

pico_sdk_path = Path(
    os.environ.get("PICO_SDK_PATH", root.parent / "pico-sdk")
).resolve()
pico_sdk_sha = git_in(pico_sdk_path, "rev-parse", "HEAD")
btstack_sha = git_in(pico_sdk_path / "lib/btstack", "rev-parse", "HEAD")
cyw43_sha = git_in(pico_sdk_path / "lib/cyw43-driver", "rev-parse", "HEAD")

manifest = {
    "product": "OAG Abo Gemi Ultra Gaming",
    "firmware_family": "OAG Universal Input Dongle",
    "phase": "U10C",
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
        "bluetooth": True,
        "bluetooth_u8_runtime_removed": True,
        "wired_baseline_restored_from": "U6E",
        "bluetooth_host_v2": True,
        "bluetooth_host_only": True,
        "bluetooth_peripheral_output": False,
        "bluetooth_max_simultaneous_peers": 4,
        "bluetooth_ble_hid_host": True,
        "bluetooth_classic_hid_host": True,
        "bluetooth_gamepad_input": True,
        "bluetooth_keyboard_input": True,
        "bluetooth_mouse_input": True,
        "bluetooth_g2e3_transport_sequence": True,
        "bluetooth_init_after_usb_host_ms": 100,
        "bluetooth_fresh_bond_reset_once": True,
        "bluetooth_historical_hidmaster_compatibility": True,
        "bluetooth_local_gap_att_server": True,
        "bluetooth_scan_uuid_1812_only": True,
        "bluetooth_scan_type": "passive",
        "bluetooth_scan_interval": 75,
        "bluetooth_scan_window": 50,
        "bluetooth_deferred_connect_outside_adv_callback": True,
        "bluetooth_u9c_bond_reset_version": 2,
        "bluetooth_u9d_exact_stack_generation": True,
        "bluetooth_hids_api": "hids_host",
        "bluetooth_sdk_23_migration": True,
        "bluetooth_u9d_bond_reset_version": 3,
        "bluetooth_u9e_live_hid_report_dispatch_fix": True,
        "bluetooth_hids_host_accepts_gattservice_packet_type": True,
        "bluetooth_u9f_baseline_freeze": True,
        "u9f_functional_source_frozen_to_u9e": True,
        "bluetooth_u9g_consumer_home_0223_guide": True,
        "bluetooth_u9g_xbox_ble_rumble_report_id": 3,
        "bluetooth_u9g_xbox_ble_rumble_retry_ms": 50,
        "bluetooth_u9g_family_scoped_feedback": True,
        "bluetooth_u10a_continuous_discovery_guard": True,
        "bluetooth_u10a_bt_first_pc_output_routing": True,
        "bluetooth_u10a_primary_bt_gamepad_sticky": True,
        "bluetooth_u10a_max_peers": 4,
        "u10c_u10a_pc_output_descriptor_restored": True,
        "u10c_primary_select_hold_ms": 3000,
        "u10c_primary_select_start_share_back_fallback": True,
        "u10c_xbox_ble_consumer_record_share": True,
        "u10c_keyboard_mouse_follow_primary": True,
        "u10c_usb_hub_gamepads_not_port_bound": True,
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
        "pico_sdk_repo": "raspberrypi/pico-sdk",
        "pico_sdk": pico_sdk_sha,
        "btstack_repo": "bluekitchen/btstack",
        "btstack": btstack_sha,
        "cyw43_driver_repo": "georgerobotics/cyw43-driver",
        "cyw43_driver": cyw43_sha,
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
        "hardware_verified_baseline_phase": "U9G",
        "hardware_verified_baseline_sha": "3fffa9484f159df6cfbeb807c3acbdf46bdbe52e",
        "u9f_simultaneous_wired_bluetooth_regression": "HARDWARE_VERIFIED",
        "u9g_xbox_ble_guide_rumble": "HARDWARE_VERIFIED",
        "u10a_single_bluetooth_primary_plus_wired_gamepad_keyboard_mouse": "HARDWARE_VERIFIED",
        "u10a_multibluetooth_2_to_4_peers": "PENDING_HARDWARE",
        "u10b_receiver_descriptor_experiment": "REJECTED_RETURNED_TO_U10A",
        "u10c_primary_selection_chord": "PENDING_HARDWARE",
    },
}

expected_pio = "5a37a66dc5d3fbe0ef3cdbeda923a757440f984f"
expected_tinyusb = "9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d"
expected_pico_sdk = "98a542c1a62fb549ffb5d66a3e5892b06276b670"
expected_btstack = "075a0780f0fad7ff67d58ac19f46e8953656a752"
expected_cyw43 = "055d64274b014dd7b1c2fc94d26e8a18face7124"

if pio_sha != expected_pio:
    raise SystemExit(
        f"manifest dependency mismatch: pico_pio_usb {pio_sha} != {expected_pio}"
    )

if tinyusb_sha != expected_tinyusb:
    raise SystemExit(
        f"manifest dependency mismatch: tinyusb {tinyusb_sha} != {expected_tinyusb}"
    )

if pico_sdk_sha != expected_pico_sdk:
    raise SystemExit(
        f"manifest dependency mismatch: pico-sdk {pico_sdk_sha} != {expected_pico_sdk}"
    )

if btstack_sha != expected_btstack:
    raise SystemExit(
        f"manifest dependency mismatch: btstack {btstack_sha} != {expected_btstack}"
    )

if cyw43_sha != expected_cyw43:
    raise SystemExit(
        f"manifest dependency mismatch: cyw43 {cyw43_sha} != {expected_cyw43}"
    )

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(
    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(f"OAG_BUILD_MANIFEST={output}")
print(f"OAG_MANIFEST_PIO_SHA={pio_sha}")
print(f"OAG_MANIFEST_TINYUSB_SHA={tinyusb_sha}")
