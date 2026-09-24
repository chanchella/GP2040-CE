#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 1
#endif

#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_DEBUG 0

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0

// TinyUSB 0.17's device stack cannot compile with an empty built-in driver
// table. Keep one Vendor driver as a compile anchor. OAG's custom XInput
// application driver is registered first and claims all four XInput vendor
// interfaces, so this built-in driver is not the gameplay transport.
#define CFG_TUD_VENDOR 1

// U2E device-side output is a custom Xbox 360/XInput-compatible vendor
// interface. Generic HID device mode is intentionally disabled.
#define CFG_TUD_HID 0

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 4

#define CFG_TUH_HID 4
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_XINPUT 4
#define CFG_TUH_XINPUT_EPIN_BUFSIZE 64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifdef __cplusplus
}
#endif
