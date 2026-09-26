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
    "product": "AOG Abo Gemi ultra gaming",
    "firmware_family": "OAG Universal Input Dongle",
    "phase": "U10F-PM1-UI5K-BT-OUT12-ARDUINO-ATT",
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
        "mouse_aim_recenter_us": 10000,
        "pc_generic_hid_output": False,
        "pc_generic_hid_output_slots": 0,
        "pc_generic_hid_axes": 6,
        "pc_generic_hid_buttons": 16,
        "pc_generic_hid_hat_switch": True,
        "pc_generic_hid_poll_interval_ms": 1,
        "pc_generic_hid_vid": "0xCAFE",
        "pc_generic_hid_pid": "0x4012",
        "pc_xinput_output": True,
        "pc_xinput_output_slots": 4,
        "multi_xinput_output": True,
        "slot0_keyboard_mouse_overlay": False,
        "host_player1_keyboard_mouse_overlay": True,
        "slot_scoped_rumble_reverse": True,
        "rumble_reverse_runtime": True,
        "bluetooth": True,
        "bluetooth_u8_runtime_removed": True,
        "wired_baseline_restored_from": "U6E",
        "bluetooth_host_v2": True,
        "bluetooth_host_only": False,
        "bluetooth_peripheral_output": True,
        "bluetooth_peripheral_transport": "BLE_HID_OVER_GATT",
        "bluetooth_peripheral_profile": "PURE_GENERIC_BLE_HID_GAMEPAD",
        "bluetooth_peripheral_input_report_count": 1,
        "bluetooth_peripheral_report_bytes": 17,
        "bluetooth_peripheral_reference": "ARDUINO_PICO_JOYSTICKBLE_GAMEPAD16",
        "bluetooth_peripheral_btstack_async_lock": True,
        "bluetooth_peripheral_registered_before_hci_power_on": True,
        "bluetooth_host_split_stack_setup_and_power_on": True,
        "bluetooth_peripheral_pnp_vid": "CAFE",
        "bluetooth_peripheral_pnp_pid": "4016",
        "bluetooth_peripheral_dual_connection_event_capture": True,
        "bluetooth_peripheral_hids_handle_adoption": True,
        "bluetooth_peripheral_primary_only": True,
        "bluetooth_peripheral_services": ["HID", "Battery", "Device Information"],
        "bluetooth_peripheral_sm_policy": "SHARED_SC_BOND_NO_IO_PREPOWER_PERSISTENT",
        "bluetooth_shared_sm_secure_connections_prepower": True,
        "bluetooth_shared_sm_secure_connections_persistent": True,
        "bluetooth_connection_selftest": True,
        "bluetooth_connection_selftest_step_ms": 900,
        "bluetooth_connection_selftest_steps": 6,
        "bluetooth_connection_selftest_sequence": ["SOUTH_DOWN", "NEUTRAL", "DPAD_DOWN", "NEUTRAL", "LX_FULL_RIGHT", "NEUTRAL"],
        "bluetooth_connection_selftest_then_live_primary": True,
        "bluetooth_phone_first_bootstrap": True,
        "bluetooth_input_discovery_locked_until_platform_hids_subscription": True,
        "bluetooth_input_discovery_unlock_event": "DISABLED_IN_OUT11",
        "bluetooth_input_host_isolated": True,
        "bluetooth_ble_hids_host_initialized": False,
        "bluetooth_classic_hid_host_initialized": False,
        "bluetooth_gatt_client_initialized": False,
        "bluetooth_usb_pio_input_still_enabled": True,
        "bluetooth_peripheral_att_profile": "ARDUINO_PICO_JOYSTICKBLE_COMPAT",
        "bluetooth_peripheral_hids_storage_reports": 2,
        "bluetooth_peripheral_get_report_callback": False,
        "bluetooth_peripheral_boot_characteristics_present": True,
        "bluetooth_peripheral_feature_report_characteristic_present": True,
        "bluetooth_ctkd": False,
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
        "u10d_independent_xinput_interfaces": True,
        "u10d_custom_descriptor_type": "0x22",
        "u10d_sequential_endpoint_pairs": ["81/01", "82/02", "83/03", "84/04"],
        "u10d_fresh_windows_serial_namespace": "OAG-IND",
        "u10d_u10c_runtime_routing_frozen": True,
        "u10e_receiver_vid": "0x045E",
        "u10e_receiver_pid": "0x0719",
        "u10e_true_receiver_interfaces": 8,
        "u10e_gamepad_protocol": "0x81",
        "u10e_aux_protocol": "0x82",
        "u10e_gamepad_endpoint_pairs": ["81/01", "83/03", "85/05", "87/07"],
        "u10e_aux_endpoint_pairs": ["82/02", "84/04", "86/06", "88/08"],
        "u10e_presence_heartbeat_ms": 1000,
        "u10e_wireless_input_packet_size": 29,
        "u10e_receiver_rumble_decode": True,
        "u10e_receiver_battery_reply": True,
        "u10e_unique_vendor_serial_reply": True,
        "u10e_u10c_primary_keyboard_mouse_routing_preserved": True,
        "u10f_xusb22_led_player_assignment_tracking": True,
        "u10f_host_player1_primary_routing": True,
        "u10f_keyboard_mouse_follow_host_player1": True,
        "u10f_primary_chord_guide_fallback": True,
        "u10f_receiver_persona_frozen_from_u10e": True,
        "u10f_pm1_exact_u10f_runtime_base": True,
        "u10f_pm1_pairing_source": "U10J",
        "u10f_pm1_pairing_assist_on_usb_detach_only": True,
        "u10f_pm1_pairing_assist_ble_only_window_ms": 30000,
        "u10f_pm1_pairing_assist_scan_type": "passive",
        "u10f_pm1_existing_bluetooth_peers_preserved": True,
        "u10f_pm1_pin_or_key_missing_single_bond_repair": True,
        "u10f_pm1_usb_host_exact_u10f": True,
        "u10f_pm1_receiver_exact_u10f": True,
        "u10f_pm1_pc_gamepad_output": "XBOX360_XINPUT_RECEIVER",
        "u10f_pm1_pc_generic_hid_gamepad_output": False,
        "u10f_pm1_supported_gamepads_normalized_to_xbox360": True,
        "pc_hid1_true_golden_base": "59182f30c2028f77c059666d6473487d90a82b64",
        "pc_hid1_runtime_delta_scope": "TARGET_FACING_PC_USB_ONLY",
        "pc_hid1_product_string": "6 axis 16 button gamepad with hat switch",
        "pc_hid2_product_string": "AOG Abo Gemi ultra gaming",
        "pc_hid2_windows_hid_class": True,
        "pc_hid2_button_order": "A_B_X_Y_LB_RB_BACK_START_L3_R3_GUIDE_SHARE",
        "pc_hid2_cross_family_semantics": "XBOX_PLAYSTATION_GENERIC_CANONICAL",
        "pc_hid2_axis_layout": "X_Y_LEFT__RX_RY_RIGHT__Z_RZ_LT_RT",
        "pc_hid2_pid_cache_bump": "0x4012",
        "universal_input_ui1": True,
        "usb_report_protocol_keyboard": True,
        "usb_report_protocol_mouse": True,
        "usb_composite_hid_descriptor_routing": True,
        "usb_hid_digital_dpad_usages": True,
        "usb_hid_consumer_home_share": True,
        "usb_hid_auto_button_layout": True,
        "usb_hid_sony_button_layout": True,
        "usb_hid_aog_loopback_layout": True,
        "usb_hid_descriptor_buffer_bytes_per_device": 1024,
        "input_support_model": "PROTOCOL_FAMILY_PLUS_DESCRIPTOR_PLUS_QUIRK",
        "input_device_coverage_goal": "1000_PLUS_DEVICE_VARIANTS",
        "razer_xbox_family_input_profiles": True,
        "razer_wolverine_v3_pro_1532_0a3f": True,
        "razer_wolverine_v3_tournament_1532_0a43": True,
        "razer_wolverine_v2_1532_0a29": True,
        "razer_wildcat_1532_0a03": True,
        "razer_atrox_1532_0a00": True,
        "easysmx_x15_receiver_1a34_f517": True,
        "easysmx_legacy_receiver_2f24_0091": True,
        "easysmx_generic_hid_bluetooth_fallback": True,
        "km_primary_extra_bind_slots": 6,
        "km_primary_extra_keyboard_slots": 4,
        "km_primary_extra_mouse_slots": 2,
        "km_extra_default_keyboard_usages": ["1", "2", "3", "4"],
        "km_extra_default_mouse_buttons": ["BACK", "FORWARD"],
        "km_extra_runtime_reconfigurable_in_mapper": True,
        "km_mouse_sensitivity_x": 1.0,
        "km_mouse_sensitivity_y": 1.0,
        "km_mouse_response_exponent": 0.58,
        "km_mouse_anti_deadzone_x": 0.0,
        "km_mouse_anti_deadzone_y": 0.0,
        "km_mouse_overlay_primary_only": True,
        "km_global_mode_toggle": "F4+F5",
        "km_global_mode_hold_ms": 2000,
        "km_default_mode": "NATIVE_KEYBOARD_MOUSE",
        "km_reserved_chord_never_forwarded": True,
        "pc_native_keyboard_output": True,
        "pc_native_mouse_output": True,
        "pc_native_hid_interfaces": 2,
        "windows_usb_parent_composite": True,
        "windows_usb_vid": "CAFE",
        "windows_usb_pid": "4016",
        "windows_xusb20_compatible_id": True,
        "windows_xusb20_receiver_interfaces": 8,
        "windows_xusb20_function_count": 4,
        "windows_xusb20_interfaces_per_function": 2,
        "windows_xusb20_function_first_interfaces": [0, 2, 4, 6],
        "windows_hid_interfaces": 2,
        "windows_usb_mode_switch_reenumerates": False,
        "native_km_combo_engine": True,
        "native_km_combo_slots": 8,
        "pc_generic_hid_profile_runtime": False,
        "android_generic_hid_profile_runtime": False,
        "ios_generic_hid_profile_foundation": True,
        "pc_hid1_generic_hid_rumble": False,
        "authentication": False,
    },
    "pc_generic_hid_profile": {
        "development_vid": "0xCAFE",
        "development_pid": "0x4012",
        "product_string": "AOG Abo Gemi ultra gaming",
        "interfaces": 4,
        "axes_per_interface": 6,
        "buttons_per_interface": 16,
        "hat_switch": True,
        "poll_interval_ms": 1,
        "xinput_rumble": False
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
        "u10c_primary_selection_chord": "HARDWARE_VERIFIED",
        "u10d_independent_controller_output": "REJECTED_HARDWARE",
        "u10e_true_xbox360_wireless_receiver": "PARTIAL_HARDWARE_VERIFIED_PRIMARY_NUMBERING_MISMATCH",
        "u10f_host_player1_primary_alignment": "HARDWARE_VERIFIED_BY_USER",
        "u10f_pm1_u10f_runtime_regression": "PENDING_HARDWARE",
        "u10f_pm1_pairing_mode": "PENDING_HARDWARE",
        "u10f_pm1_usb_three_roots": "PENDING_HARDWARE",
        "u10f_pm1_xbox360_output_policy": "HISTORICAL_TRUE_GOLDEN",
        "u10f_pm1_pc_hid1": "HARDWARE_VERIFIED_ENUMERATION_BY_USER",
        "u10f_pm1_pc_hid2_universal_map": "HARDWARE_ENUMERATION_VERIFIED_MAPPING_PENDING",
        "u10f_pm1_ui1_universal_input": "HARDWARE_ENUMERATION_VERIFIED_INPUT_EXPANSION_PENDING",
        "u10f_pm1_ui2_pc_xinput": "PENDING_HARDWARE",
        "u10f_pm1_ui3_razer_easysmx_input": "PENDING_HARDWARE",
        "u10f_pm1_ui4_km_extra6_mouse_aim": "PENDING_HARDWARE",
        "u10f_pm1_ui4b_km_extra6_mouse_aim_strong": "PENDING_HARDWARE",
        "u10f_pm1_ui4c_km_extra6_mouse_aim_ultra": "PENDING_HARDWARE",
        "u10f_pm1_ui4d_km_extra6_mouse_aim_60": "PENDING_HARDWARE",
        "u10f_pm1_ui4e_km_extra6_mouse_aim_100_6ms": "PENDING_HARDWARE",
        "u10f_pm1_ui4f_km_extra6_mouse_aim_100_10ms": "PENDING_HARDWARE",
        "u10f_pm1_ui5a_global_km_modes_pc_native": "PARTIAL_HARDWARE_KM_AS_CONTROLLER_RESPONDED_NATIVE_KM_FAILED",
        "u10f_pm1_ui5b_global_km_hard_switch": "REJECTED_HARDWARE_BY_USER",
        "u10f_pm1_ui5c_global_km_logical_switch": "REJECTED_HARDWARE_NATIVE_KM_FAILED",
        "u10f_pm1_ui5d_dual_usb_persona_2s": "SUPERSEDED_BEFORE_HARDWARE",
        "u10f_pm1_ui5e_stable_composite_km_2s": "REJECTED_HARDWARE_CONTROLLERS_NOT_ENUMERATED",
        "u10f_pm1_ui5f_recovery_ui5a_2s": "HARDWARE_VERIFIED_CONTROLLERS_AND_KM_CONTROLLER_MODE_BY_USER",
        "u10f_pm1_ui5g_win_xusb20_km_composite_2s": "PARTIAL_HARDWARE_NATIVE_KM_AND_BT_CONTROLLER_WORKING_OTHER_CONTROLLERS_NOT_VISIBLE_BY_USER",
        "u10f_pm1_ui5j_win_xusb20_iad8_km_composite_2s": "REJECTED_HARDWARE_NO_CONTROLLERS_NO_KEYBOARD_NO_MOUSE_BY_USER",
        "u10f_pm1_ui5k_win_xusb20_4func_km_composite_2s": "HARDWARE_VERIFIED_BY_USER",
        "u10f_pm1_ui5k_bt_out1": "HARDWARE_CONNECTED_NO_INPUT",
        "u10f_pm1_ui5k_bt_out2": "HARDWARE_CONNECTED_NO_INPUT",
        "u10f_pm1_ui5k_bt_out3": "HARDWARE_CONNECTED_NO_INPUT",
        "u10f_pm1_ui5k_bt_out4_diag": "HARDWARE_LED_NO_SIGNAL",
        "u10f_pm1_ui5k_bt_out5_handle_fix": "HARDWARE_CONNECTED_NO_INPUT",
        "u10f_pm1_ui5k_bt_out6_arduino_reference": "HARDWARE_CONNECTED_NO_INPUT",
        "standalone_arduino_pico_joystickble_probe": "HARDWARE_PASS",
        "u10f_pm1_ui5k_bt_out7_pre_power": "HARDWARE_CONNECTED_AFTER_PHONE_BT_RESET_NO_INPUT",
        "u10f_pm1_ui5k_bt_out8_secure_pre_power": "HARDWARE_CONNECTED_NO_INPUT",
        "u10f_pm1_ui5k_bt_out9_selftest_live": "HARDWARE_CONNECTED_NO_SELFTEST_NO_INPUT",
        "u10f_pm1_ui5k_bt_out10_phone_first": "HARDWARE_CONNECTED_NO_SELFTEST_NO_INPUT",
        "u10f_pm1_ui5k_bt_out11_output_isolation": "HARDWARE_CONNECTED_NO_SELFTEST_NO_INPUT",
        "u10f_pm1_ui5k_bt_out12_arduino_att": "PENDING_HARDWARE",
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
