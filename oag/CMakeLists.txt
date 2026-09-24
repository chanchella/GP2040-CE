cmake_minimum_required(VERSION 3.20)

project(OAG_Universal_Core_V1 LANGUAGES CXX)

option(OAG_BUILD_HOST_TESTS "Build host-side OAG core tests" ON)

add_library(oag_core STATIC
    src/device/device_registry.cpp
    src/device/usb_device_classifier.cpp
    src/protocol/xusb/xusb_input_driver.cpp
    src/protocol/xgip/xgip_input_driver.cpp
    src/mapping/logical_slot_manager.cpp
    src/mapping/pass_through_mapping.cpp
    src/output/xinput/xinput_report_encoder.cpp
    src/output/xinput/xinput_feedback_decoder.cpp
    src/feedback/keyboard_led_state.cpp
    src/mapping/mouse_to_stick_mapper.cpp
    src/protocol/hid/boot_keyboard_input_driver.cpp
    src/protocol/hid/boot_mouse_input_driver.cpp
    src/mapping/digital_binding_engine.cpp
    src/output/platform/platform_profile_registry.cpp
    src/protocol/hid/generic_hid_gamepad_driver.cpp
    src/mapping/keyboard_mouse_gamepad_mapper.cpp
    src/output/platform/switch/switch_pro_report_encoder.cpp
    src/output/platform/playstation/ps3_report_encoder.cpp
    src/output/platform/playstation/ps3_feedback_decoder.cpp
    src/output/platform/xbox/xgip_packet.cpp
    src/output/platform/xbox/xbox_gip_report_encoder.cpp
    src/output/platform/playstation/ps4_report_encoder.cpp
    src/output/platform/playstation/ps4_feedback_decoder.cpp
    src/output/platform/platform_selector.cpp
    src/output/platform/platform_activation_gate.cpp
    src/bluetooth/bluetooth_device_classifier.cpp
    src/output/universal_output_mode.cpp
    src/mapping/touchscreen_mapper.cpp
    src/mapping/pen_tablet_mapper.cpp
)

target_include_directories(oag_core
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(oag_core PUBLIC cxx_std_17)

target_compile_options(oag_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

if(OAG_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(oag_core_tests
        tests/host/test_core.cpp
    )

    target_link_libraries(oag_core_tests PRIVATE oag_core)
    target_compile_features(oag_core_tests PRIVATE cxx_std_17)

    add_test(NAME oag_core_tests COMMAND oag_core_tests)

    add_executable(oag_mouse_mapping_tests
        tests/host/test_mouse_mapping.cpp
    )

    target_link_libraries(oag_mouse_mapping_tests PRIVATE oag_core)
    target_compile_features(oag_mouse_mapping_tests PRIVATE cxx_std_17)

    add_test(NAME oag_mouse_mapping_tests COMMAND oag_mouse_mapping_tests)

    add_executable(oag_hid_boot_parser_tests
        tests/host/test_hid_boot_parsers.cpp
    )

    target_link_libraries(oag_hid_boot_parser_tests PRIVATE oag_core)
    target_compile_features(oag_hid_boot_parser_tests PRIVATE cxx_std_17)

    add_test(NAME oag_hid_boot_parser_tests COMMAND oag_hid_boot_parser_tests)

    add_executable(oag_digital_binding_tests
        tests/host/test_digital_binding_engine.cpp
    )

    target_link_libraries(oag_digital_binding_tests PRIVATE oag_core)
    target_compile_features(oag_digital_binding_tests PRIVATE cxx_std_17)

    add_test(NAME oag_digital_binding_tests COMMAND oag_digital_binding_tests)

    add_executable(oag_platform_profile_tests
        tests/host/test_platform_profiles.cpp
    )

    target_link_libraries(oag_platform_profile_tests PRIVATE oag_core)
    target_compile_features(oag_platform_profile_tests PRIVATE cxx_std_17)

    add_test(NAME oag_platform_profile_tests COMMAND oag_platform_profile_tests)

    add_executable(oag_generic_hid_gamepad_tests
        tests/host/test_generic_hid_gamepad.cpp
    )

    target_link_libraries(oag_generic_hid_gamepad_tests PRIVATE oag_core)
    target_compile_features(oag_generic_hid_gamepad_tests PRIVATE cxx_std_17)

    add_test(NAME oag_generic_hid_gamepad_tests COMMAND oag_generic_hid_gamepad_tests)

    add_executable(oag_keyboard_mouse_gamepad_tests
        tests/host/test_keyboard_mouse_gamepad_mapper.cpp
    )
    target_link_libraries(oag_keyboard_mouse_gamepad_tests PRIVATE oag_core)
    target_compile_features(oag_keyboard_mouse_gamepad_tests PRIVATE cxx_std_17)
    add_test(NAME oag_keyboard_mouse_gamepad_tests COMMAND oag_keyboard_mouse_gamepad_tests)

    add_executable(oag_switch_pro_encoder_tests
        tests/host/test_switch_pro_report_encoder.cpp
    )
    target_link_libraries(oag_switch_pro_encoder_tests PRIVATE oag_core)
    target_compile_features(oag_switch_pro_encoder_tests PRIVATE cxx_std_17)
    add_test(NAME oag_switch_pro_encoder_tests COMMAND oag_switch_pro_encoder_tests)

    add_executable(oag_ps3_encoder_tests
        tests/host/test_ps3_report_encoder.cpp
    )
    target_link_libraries(oag_ps3_encoder_tests PRIVATE oag_core)
    target_compile_features(oag_ps3_encoder_tests PRIVATE cxx_std_17)
    add_test(NAME oag_ps3_encoder_tests COMMAND oag_ps3_encoder_tests)

    add_executable(oag_xbox_gip_encoder_tests
        tests/host/test_xbox_gip_report_encoder.cpp
    )
    target_link_libraries(oag_xbox_gip_encoder_tests PRIVATE oag_core)
    target_compile_features(oag_xbox_gip_encoder_tests PRIVATE cxx_std_17)
    add_test(NAME oag_xbox_gip_encoder_tests COMMAND oag_xbox_gip_encoder_tests)

    add_executable(oag_ps4_encoder_tests
        tests/host/test_ps4_report_encoder.cpp
    )
    target_link_libraries(oag_ps4_encoder_tests PRIVATE oag_core)
    target_compile_features(oag_ps4_encoder_tests PRIVATE cxx_std_17)
    add_test(NAME oag_ps4_encoder_tests COMMAND oag_ps4_encoder_tests)

    add_executable(oag_platform_selection_tests
        tests/host/test_platform_selection.cpp
    )
    target_link_libraries(oag_platform_selection_tests PRIVATE oag_core)
    target_compile_features(oag_platform_selection_tests PRIVATE cxx_std_17)
    add_test(NAME oag_platform_selection_tests COMMAND oag_platform_selection_tests)

    add_executable(oag_usb_classifier_xgip_tests
        tests/host/test_usb_classifier_xgip.cpp
    )
    target_link_libraries(oag_usb_classifier_xgip_tests PRIVATE oag_core)
    target_compile_features(oag_usb_classifier_xgip_tests PRIVATE cxx_std_17)
    add_test(NAME oag_usb_classifier_xgip_tests COMMAND oag_usb_classifier_xgip_tests)

    add_executable(oag_bluetooth_classifier_tests
        tests/host/test_bluetooth_classifier.cpp
    )
    target_link_libraries(oag_bluetooth_classifier_tests PRIVATE oag_core)
    target_compile_features(oag_bluetooth_classifier_tests PRIVATE cxx_std_17)
    add_test(NAME oag_bluetooth_classifier_tests COMMAND oag_bluetooth_classifier_tests)

    add_executable(oag_keyboard_led_tests
        tests/host/test_keyboard_led_state.cpp
    )
    target_link_libraries(oag_keyboard_led_tests PRIVATE oag_core)
    target_compile_features(oag_keyboard_led_tests PRIVATE cxx_std_17)
    add_test(NAME oag_keyboard_led_tests COMMAND oag_keyboard_led_tests)

    add_executable(oag_output_mode_tests
        tests/host/test_output_modes.cpp
    )
    target_link_libraries(oag_output_mode_tests PRIVATE oag_core)
    target_compile_features(oag_output_mode_tests PRIVATE cxx_std_17)
    add_test(NAME oag_output_mode_tests COMMAND oag_output_mode_tests)
endif()
