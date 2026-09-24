#pragma once

#include <cstdint>

#include "oag/auth/auth_requirement.h"

namespace oag {

enum class PlatformId : std::uint8_t {
    PcXinput360 = 0,
    PcGenericHid,
    AndroidGamepad,
    IosGameController,
    NintendoSwitch,
    Playstation3,
    Playstation4,
    Playstation5,
    Xbox360Console,
    XboxOneConsole,
    XboxSeriesConsole,
};

enum class PlatformWireProtocol : std::uint8_t {
    XusbXbox360 = 0,
    GenericHid,
    NintendoSwitchHid,
    Playstation3Hid,
    Playstation4Hid,
    Playstation5Hid,
    Xbox360Console,
    XgipXboxOneSeries,
    MobileHid,
};

enum class PlatformImplementationStatus : std::uint8_t {
    Planned = 0,
    SoftwareFoundation,
    RuntimeAvailable,
};

struct PlatformProfile {
    PlatformId id = PlatformId::PcXinput360;
    PlatformWireProtocol wireProtocol =
        PlatformWireProtocol::XusbXbox360;
    PlatformImplementationStatus implementation =
        PlatformImplementationStatus::Planned;
    AuthRequirement auth {};
    std::uint8_t maxLogicalGamepads = 1;
    bool supportsRumble = false;
    bool supportsTriggerRumble = false;
    bool supportsMotion = false;
    bool supportsTouch = false;
    bool hardwareVerified = false;
};

} // namespace oag
