#pragma once

#include <cstdint>

#include "tusb.h"

namespace oag::firmware {

bool handleCompositeXusbOsDescriptorRequest(
    std::uint8_t rhport,
    tusb_control_request_t const* request
);

} // namespace oag::firmware
