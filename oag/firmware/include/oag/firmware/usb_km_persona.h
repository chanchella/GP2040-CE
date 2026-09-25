#pragma once

namespace oag::firmware {

// Controls whether the target-facing USB configuration exposes the native
// keyboard and mouse HID interfaces. The Xbox 360 receiver interfaces remain
// present in both modes.
void setNativeKmUsbExposed(bool exposed);

bool nativeKmUsbExposed();

} // namespace oag::firmware
