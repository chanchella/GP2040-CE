#pragma once

#include <cstdint>

#include "tusb.h"

namespace oag::firmware {

// Handles the Microsoft OS 1.0 Extended Compatible ID request used by
// Windows to bind the first eight interfaces to the in-box XUSB20 driver
// while leaving the HID keyboard/mouse interfaces under hidclass.
bool handleWindowsXusb20CompatIdRequest(
    std::uint8_t rhport,
    tusb_control_request_t const* request
);

} // namespace oag::firmware
