#pragma once

#include <cstdint>

namespace oag::firmware {

enum class TargetUsbPersona : std::uint8_t {
    NativeKeyboardMouse = 0,
    Xbox360Receiver,
};

void setTargetUsbPersona(TargetUsbPersona persona);

TargetUsbPersona targetUsbPersona();

} // namespace oag::firmware
