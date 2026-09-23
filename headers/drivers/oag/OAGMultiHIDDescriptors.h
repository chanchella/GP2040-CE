/*
 * SPDX-License-Identifier: MIT
 *
 * OAG Vortex multi-gamepad HID descriptor set.
 * Four independent standard HID gamepad interfaces are exposed so
 * browser Gamepad API / RawInput-style consumers can enumerate each
 * logical UniversalOutput slot independently.
 */

#pragma once

#include <stdint.h>

#include "device/oag_identity.h"

static constexpr uint8_t OAG_MULTI_HID_SLOT_COUNT = 4;
static constexpr uint8_t OAG_MULTI_HID_KEYBOARD_INTERFACE = 4;
static constexpr uint8_t OAG_MULTI_HID_MOUSE_INTERFACE = 5;
static constexpr uint8_t OAG_MULTI_HID_INTERFACE_COUNT = 6;
static constexpr uint8_t OAG_MULTI_HID_ENDPOINT_SIZE = 64;
static constexpr uint16_t OAG_MULTI_HID_REPORT_DESC_SIZE = 101;
static constexpr uint16_t OAG_MULTI_HID_CONFIG_SIZE =
    9 + (OAG_MULTI_HID_INTERFACE_COUNT * (9 + 9 + 7));

static constexpr uint8_t OAG_HID_REPORT_ID_KEYBOARD = 1;
static constexpr uint8_t OAG_HID_REPORT_ID_CONSUMER = 2;
static constexpr uint8_t OAG_HID_REPORT_ID_MOUSE = 3;

struct __attribute__((packed, aligned(1))) OAGMultiHIDReport {
    uint32_t buttons;
    uint8_t hat;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
};

static_assert(sizeof(OAGMultiHIDReport) == 9, "OAG HID input report size changed");

struct __attribute__((packed, aligned(1))) OAGMultiHIDFeedbackReport {
    uint8_t leftMotor;
    uint8_t rightMotor;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
};

static_assert(sizeof(OAGMultiHIDFeedbackReport) == 4, "OAG feedback report size changed");

struct __attribute__((packed, aligned(1))) OAGKeyboardReport {
    uint32_t keys[8];
};
static_assert(sizeof(OAGKeyboardReport) == 32, "OAG keyboard report size changed");

struct __attribute__((packed, aligned(1))) OAGConsumerReport {
    uint16_t usages;
};
static_assert(sizeof(OAGConsumerReport) == 2, "OAG consumer report size changed");

struct __attribute__((packed, aligned(1))) OAGMouseReport {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t wheel;
    int8_t pan;
};
static_assert(sizeof(OAGMouseReport) == 7, "OAG mouse report size changed");

static const uint8_t oag_multi_hid_report_descriptor[] = {
    // Input section intentionally mirrors GP2040-CE's proven Generic HID
    // descriptor so Windows/Chromium see a conventional gamepad.
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // Buttons 1..16 keep their existing bit positions.
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x10,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x10,
    0x75, 0x01,
    0x81, 0x02,

    // Bit 16: semantic Home / Guide button.
    // Chromium/Windows RawInput recognizes System Main Menu as a special
    // gamepad button; this is also used by real controllers for Guide/Home.
    0x05, 0x01,
    0x09, 0x85,        // System Main Menu
    0x95, 0x01,
    0x75, 0x01,
    0x81, 0x02,

    // Bits 17..31 remain Buttons 18..32, preserving A2/A3/A4/E1..E12.
    0x05, 0x09,
    0x19, 0x12,
    0x29, 0x20,
    0x95, 0x0F,
    0x75, 0x01,
    0x81, 0x02,

    // Hat switch
    0x05, 0x01,
    0x09, 0x39,
    0x25, 0x07,
    0x95, 0x01,
    0x75, 0x04,
    0x81, 0x42,

    // Hat padding
    0x95, 0x01,
    0x75, 0x04,
    0x81, 0x01,

    // Four axes, matching GP2040 Generic HID:
    // X/Y = left stick, Z/Rz = right stick.
    0x05, 0x01,
    0x26, 0xFF, 0x00,
    0x46, 0xFF, 0x00,
    0x09, 0x30,        // X
    0x09, 0x31,        // Y
    0x09, 0x32,        // Z
    0x09, 0x35,        // Rz
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,

    // Keep the already hardware-proven OAG 4-byte feedback path.
    0x06, 0x00, 0xFF,
    0x09, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x04,
    0x91, 0x02,

    0xC0
};

static_assert(
    sizeof(oag_multi_hid_report_descriptor) == OAG_MULTI_HID_REPORT_DESC_SIZE,
    "OAG multi HID report descriptor size changed"
);

// Keyboard + media-key HID interface.
// Keyboard uses a 256-bit NKRO bitmap. Modifier usages E0..E7 are emitted
// into the same bitmap by the driver.
static const uint8_t oag_keyboard_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, OAG_HID_REPORT_ID_KEYBOARD,
    0x05, 0x07,        // Usage Page (Keyboard/Keypad)
    0x19, 0x00,        // Usage Minimum (0)
    0x29, 0xFF,        // Usage Maximum (255)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x96, 0x00, 0x01,  // Report Count (256)
    0x81, 0x02,        // Input (Data,Var,Abs)

    // Standard host -> keyboard LED state.
    0x05, 0x08,        // Usage Page (LEDs)
    0x19, 0x01,        // Num Lock
    0x29, 0x05,        // Kana
    0x95, 0x05,
    0x75, 0x01,
    0x91, 0x02,        // Output (Data,Var,Abs)
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,        // Output padding
    0xC0,

    // Common consumer/media controls kept on the same physical interface.
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,
    0x85, OAG_HID_REPORT_ID_CONSUMER,
    0x15, 0x00,
    0x25, 0x01,
    0x09, 0xE2,        // Mute
    0x09, 0xE9,        // Volume Up
    0x09, 0xEA,        // Volume Down
    0x09, 0xCD,        // Play/Pause
    0x09, 0xB5,        // Next Track
    0x09, 0xB6,        // Previous Track
    0x09, 0xB7,        // Stop
    0x09, 0xB8,        // Eject
    0x0A, 0x23, 0x02,  // AC Home
    0x0A, 0x21, 0x02,  // AC Search
    0x0A, 0x24, 0x02,  // AC Back
    0x0A, 0x25, 0x02,  // AC Forward
    0x0A, 0x27, 0x02,  // AC Refresh
    0x0A, 0x2A, 0x02,  // AC Bookmarks
    0x09, 0xB3,        // Fast Forward
    0x09, 0xB4,        // Rewind
    0x75, 0x01,
    0x95, 0x10,
    0x81, 0x02,
    0xC0
};

// Native mouse with 8 buttons, 16-bit relative X/Y, vertical wheel and pan.
static const uint8_t oag_mouse_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, OAG_HID_REPORT_ID_MOUSE,
    0x09, 0x01,        // Usage (Pointer)
    0xA1, 0x00,        // Collection (Physical)

    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,
    0x29, 0x08,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,

    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x30,        // X
    0x09, 0x31,        // Y
    0x16, 0x01, 0x80,  // Logical Min (-32767)
    0x26, 0xFF, 0x7F,  // Logical Max (32767)
    0x75, 0x10,
    0x95, 0x02,
    0x81, 0x06,        // Input (Data,Var,Rel)

    0x09, 0x38,        // Wheel
    0x15, 0x81,        // -127
    0x25, 0x7F,        // 127
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x06,

    0x05, 0x0C,        // Usage Page (Consumer)
    0x0A, 0x38, 0x02,  // AC Pan
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x06,

    0xC0,
    0xC0
};

static const uint8_t oag_multi_hid_device_descriptor[] = {
    0x12,       // bLength
    0x01,       // Device descriptor
    0x00, 0x02, // USB 2.00
    0x00,       // per-interface class
    0x00,
    0x00,
    0x40,       // EP0 64 bytes

    // Keep GP2040 generic-HID identity instead of impersonating a
    // proprietary console controller.
    0xC4, 0x10, // VID 10C4
    0xC0, 0x82, // PID 82C0

    0x05, 0x02, // bcdDevice 2.05
    0x01,       // manufacturer
    0x02,       // product
    0x03,       // serial
    0x01        // configurations
};
