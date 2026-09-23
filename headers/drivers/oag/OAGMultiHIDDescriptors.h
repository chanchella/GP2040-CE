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
static constexpr uint8_t OAG_MULTI_HID_ENDPOINT_SIZE = 64;
static constexpr uint16_t OAG_MULTI_HID_REPORT_DESC_SIZE = 89;
static constexpr uint16_t OAG_MULTI_HID_CONFIG_SIZE =
    9 + (OAG_MULTI_HID_SLOT_COUNT * (9 + 9 + 7));

struct __attribute__((packed, aligned(1))) OAGMultiHIDReport {
    uint32_t buttons;
    uint8_t hat;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
};

struct __attribute__((packed, aligned(1))) OAGMultiHIDFeedbackReport {
    uint8_t leftMotor;
    uint8_t rightMotor;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
};

static_assert(sizeof(OAGMultiHIDFeedbackReport) == 4, "OAG feedback report size changed");

static const uint8_t oag_multi_hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // 32 buttons
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x20,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x20,
    0x75, 0x01,
    0x81, 0x02,

    // Hat
    0x05, 0x01,
    0x09, 0x39,
    0x15, 0x00,
    0x25, 0x07,
    0x35, 0x00,
    0x46, 0x3B, 0x01,
    0x65, 0x14,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x42,

    // Hat padding
    0x65, 0x00,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x01,

    // Four analog axes
    0x05, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x09, 0x30,        // X
    0x09, 0x31,        // Y
    0x09, 0x33,        // Rx
    0x09, 0x34,        // Ry
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,

    // Per-slot PC -> OAG feedback. No Report ID: G2C1 input packet stays stable.
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

    0x01, 0x02, // bcdDevice 2.01
    0x01,       // manufacturer
    0x02,       // product
    0x03,       // serial
    0x01        // configurations
};
