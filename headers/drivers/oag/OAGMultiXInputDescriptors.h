/*
 * SPDX-License-Identifier: MIT
 *
 * OAG multi-controller XInput descriptor layout.
 *
 * The multi-interface shape is adapted from the MIT-licensed
 * CasperVM/360-w-raw-gadget project (Copyright 2024 Casper van Mourik),
 * rewritten for TinyUSB/RP2350 and the OAG slot architecture.
 */

#pragma once

#include <stdint.h>

#include "device/oag_identity.h"
#include "drivers/xinput/XInputDescriptors.h"

static constexpr uint8_t OAG_MULTI_XINPUT_SLOT_COUNT = 4;
static constexpr uint8_t OAG_MULTI_XINPUT_ENDPOINT_SIZE = 32;
static constexpr uint16_t OAG_MULTI_XINPUT_CONFIG_SIZE =
    9 + (OAG_MULTI_XINPUT_SLOT_COUNT * 43);

static const uint8_t oag_multi_xinput_device_descriptor[] = {
    0x12,       // bLength
    0x01,       // DEVICE
    0x00, 0x02, // USB 2.00
    0xFF,       // vendor-specific device class
    0xFF,
    0xFF,
    0x40,       // EP0 = 64 bytes
    0x5E, 0x04, // VID 045E
    0x8E, 0x02, // PID 028E
    0x00, 0x02, // bcdDevice 2.00
    0x01,       // manufacturer string
    0x02,       // product string
    0x03,       // serial string
    0x01,       // one configuration
};
