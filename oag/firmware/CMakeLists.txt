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

project(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W
    src/main.cpp
    src/usb_pio_host.cpp
    src/usb_pio_probe.c
    src/xinput_host.cpp
    src/pc_hid_output.cpp
    src/usb_descriptors.cpp

    ../src/device/device_registry.cpp
    ../src/protocol/xusb/xusb_input_driver.cpp
    ../src/protocol/hid/boot_keyboard_input_driver.cpp
    ../src/protocol/hid/boot_mouse_input_driver.cpp
    ../src/mapping/logical_slot_manager.cpp
    ../src/mapping/pass_through_mapping.cpp
)

target_include_directories(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../include"
)

target_compile_definitions(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    PICO_STACK_SIZE=0x1000
)

target_compile_options(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W PRIVATE
    pico_stdlib
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
)

pico_enable_stdio_usb(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W 0)
pico_enable_stdio_uart(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W 0)

pico_set_program_name(
    OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W
    "OAG Abo Gemi Ultra Gaming"
)
pico_set_program_version(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W "U1")

target_link_options(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W.map"
)

pico_add_extra_outputs(OAG_Abo_Gemi_Ultra_Gaming_U1_Pico2W)
