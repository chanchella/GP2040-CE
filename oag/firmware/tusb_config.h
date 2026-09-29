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
// The genuine Xbox 360 Wireless Receiver is full-speed with an 8-byte EP0.
#define CFG_TUD_ENDPOINT0_SIZE 8
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_AUDIO 1

// Controller-audio bridge: expose one 48 kHz / 16-bit / mono UAC2 microphone
// on the target-facing native USB link while preserving the Golden XInput
// receiver and native keyboard/mouse functions.
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE 48000
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN TUD_AUDIO_MIC_ONE_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX 2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 1
#define CFG_TUD_AUDIO_EP_SZ_IN \
    TUD_AUDIO_EP_SIZE( \
        CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE, \
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, \
        CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX \
    )
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * CFG_TUD_AUDIO_EP_SZ_IN)

// TinyUSB 0.17 compile anchor. OAG's custom XInput application driver is
// registered first and claims the target-facing XInput interfaces.
#define CFG_TUD_VENDOR 1
#define CFG_TUD_HID 6
#define CFG_TUD_HID_EP_BUFSIZE 16

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1

// U6A multi-device host budget.
// - up to 12 non-hub USB devices
// - up to 4 hubs
// - larger enumeration buffer for complex gamepad HID descriptors
#define CFG_TUH_DEVICE_MAX 12
#define CFG_TUH_HUB 4
#define CFG_TUH_ENUMERATION_BUFSIZE 1024

#define CFG_TUH_HID 12
#define CFG_TUH_HID_EPIN_BUFSIZE 128
#define CFG_TUH_HID_EPOUT_BUFSIZE 128

#define CFG_TUH_XINPUT 8
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
