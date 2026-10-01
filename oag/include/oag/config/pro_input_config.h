#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "oag/device/device_registry.h"
namespace oag {
enum class ProInputKind : std::uint8_t { Mouse, Keyboard, Gamepad };
enum class ProDpiSource : std::uint8_t { Unknown, Manual, Estimated };
struct ProInputSettings {
    std::uint32_t sourceDpi = 0, targetDpi = 1600;
    std::uint16_t multiplierPermille = 1000, gainXPermille = 1000, gainYPermille = 1000;
    std::uint16_t processingHz = 1000, curvePermille = 1000;
    std::uint16_t innerDeadzonePermille = 0, outerDeadzonePermille = 0;
    // One count preserves the existing Diamond eight-direction stick mapping.
    std::uint16_t mouseFullScaleCounts = 1;
    bool automaticMultiplier = true, fractionalRemainder = true;
    ProDpiSource dpiSource = ProDpiSource::Unknown;
    std::uint8_t reserved = 0;
};
struct ProDeviceProfile {
    bool enabled = false;
    ProInputKind kind = ProInputKind::Mouse;
    TransportType transport = TransportType::UsbPioHost;
    std::uint8_t reserved = 0;
    std::uint16_t vid = 0, pid = 0;
    ProInputSettings settings {};
};
struct ProInputConfig {
    static constexpr std::size_t kDeviceProfiles = 16;
    // V6 layout unchanged: formerly named nativeDesktop. Cancels only the
    // selected game effects; it never selects a USB profile or KM route.
    bool gameContextInactive = false;
    std::array<std::uint8_t, 3> reserved {};
    std::array<ProInputSettings, 3> defaults {};
    std::array<ProDeviceProfile, kDeviceProfiles> devices {};
};
bool validProInputSettings(const ProInputSettings& settings);
ProInputKind proInputKind(ProtocolKind protocol);
const ProInputSettings& proSettingsFor(const ProInputConfig&, const DeviceRecord&, ProInputKind);
int proProfileSlot(const ProInputConfig&, const DeviceRecord&, ProInputKind);
std::int32_t proDpiGainQ16(const ProInputSettings&);
} // namespace oag
