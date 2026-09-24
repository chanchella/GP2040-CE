cmake_minimum_required(VERSION 3.20)

set(OAG_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Target board")
endif()

if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Target platform")
endif()

set(PICO_TINYUSB_PATH "${OAG_REPO_ROOT}/lib/tinyusb" CACHE PATH "Pinned TinyUSB")
set(PICO_PIO_USB_PATH "${OAG_REPO_ROOT}/lib/pico_pio_usb" CACHE PATH "Pinned Pico-PIO-USB")

include("${OAG_REPO_ROOT}/pico_sdk_import.cmake")

project(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W
    src/main.cpp
    src/usb_pio_host.cpp
    src/usb_pio_probe.c
    src/xinput_host.cpp
    src/pc_xinput_device.cpp
    src/pc_xinput_platform_driver.cpp
    src/usb_descriptors.cpp
    src/bluetooth_runtime.cpp

    ../src/device/device_registry.cpp
    ../src/device/usb_device_classifier.cpp
    ../src/protocol/xusb/xusb_input_driver.cpp
    ../src/protocol/xgip/xgip_input_driver.cpp
    ../src/protocol/hid/boot_keyboard_input_driver.cpp
    ../src/protocol/hid/boot_mouse_input_driver.cpp
    ../src/protocol/hid/generic_hid_gamepad_driver.cpp
    ../src/mapping/logical_slot_manager.cpp
    ../src/mapping/pass_through_mapping.cpp
    ../src/mapping/digital_binding_engine.cpp
    ../src/mapping/mouse_to_stick_mapper.cpp
    ../src/mapping/keyboard_mouse_gamepad_mapper.cpp
    ../src/output/xinput/xinput_report_encoder.cpp
    ../src/output/xinput/xinput_feedback_decoder.cpp
    ../src/feedback/keyboard_led_state.cpp
    ../src/output/universal_output_mode.cpp
    ../src/mapping/touchscreen_mapper.cpp
    ../src/mapping/pen_tablet_mapper.cpp
    ../src/bluetooth/bluetooth_device_classifier.cpp
)

target_include_directories(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../include"

    # Pico SDK 2.1.1 cyw43.h references lwIP public types even with
    # CYW43_LWIP=0. Expose headers only; OAG still uses the explicit poll
    # CYW43 architecture and does not enable the Wi-Fi/lwIP runtime.
    "${PICO_SDK_PATH}/lib/lwip/src/include"
)

target_compile_definitions(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    PICO_STACK_SIZE=0x1000
    CYW43_LWIP=0
)

target_compile_options(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W PRIVATE
    pico_stdlib
    pico_unique_id
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_ble
    pico_btstack_classic
)

pico_enable_stdio_usb(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W 0)
pico_enable_stdio_uart(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W 0)

pico_set_program_name(
    OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W
    "OAG Abo Gemi Ultra Gaming"
)
pico_set_program_version(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W "U8I")

target_link_options(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W.map"
)

pico_add_extra_outputs(OAG_Abo_Gemi_Ultra_Gaming_U8I_Pico2W)
