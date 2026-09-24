#include "oag/output/platform/platform_profile_registry.h"

#include <array>
#include <cstddef>

namespace oag {
namespace {

constexpr PlatformProfile makeProfile(
    PlatformId id,
    PlatformWireProtocol protocol,
    PlatformImplementationStatus implementation,
    AuthRequirement auth,
    std::uint8_t maxLogicalGamepads,
    bool rumble,
    bool triggerRumble,
    bool motion,
    bool touch,
    bool hardwareVerified
) {
    return {
        id,
        protocol,
        implementation,
        auth,
        maxLogicalGamepads,
        rumble,
        triggerRumble,
        motion,
        touch,
        hardwareVerified,
    };
}

constexpr AuthRequirement kNoAuth {
    AuthRequirementKind::None,
    AuthDonorFamily::None,
};

constexpr std::array<PlatformProfile, 11> kProfiles {{
    makeProfile(
        PlatformId::PcXinput360,
        PlatformWireProtocol::XusbXbox360,
        PlatformImplementationStatus::RuntimeAvailable,
        kNoAuth,
        4,
        true,
        false,
        false,
        false,
        true
    ),
    makeProfile(
        PlatformId::PcGenericHid,
        PlatformWireProtocol::GenericHid,
        PlatformImplementationStatus::SoftwareFoundation,
        kNoAuth,
        4,
        false,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::AndroidGamepad,
        PlatformWireProtocol::MobileHid,
        PlatformImplementationStatus::Planned,
        kNoAuth,
        1,
        true,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::IosGameController,
        PlatformWireProtocol::MobileHid,
        PlatformImplementationStatus::Planned,
        kNoAuth,
        1,
        true,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::NintendoSwitch,
        PlatformWireProtocol::NintendoSwitchHid,
        PlatformImplementationStatus::SoftwareFoundation,
        kNoAuth,
        1,
        true,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::Playstation3,
        PlatformWireProtocol::Playstation3Hid,
        PlatformImplementationStatus::SoftwareFoundation,
        kNoAuth,
        1,
        true,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::Playstation4,
        PlatformWireProtocol::Playstation4Hid,
        PlatformImplementationStatus::Planned,
        {
            AuthRequirementKind::LiveOfficialDonorPassthrough,
            AuthDonorFamily::DualShock4,
        },
        1,
        true,
        false,
        true,
        true,
        false
    ),
    makeProfile(
        PlatformId::Playstation5,
        PlatformWireProtocol::Playstation5Hid,
        PlatformImplementationStatus::Planned,
        {
            AuthRequirementKind::LiveOfficialDonorPassthrough,
            AuthDonorFamily::DualSense,
        },
        1,
        true,
        true,
        true,
        true,
        false
    ),
    makeProfile(
        PlatformId::Xbox360Console,
        PlatformWireProtocol::Xbox360Console,
        PlatformImplementationStatus::Planned,
        {
            AuthRequirementKind::LiveOfficialDonorPassthrough,
            AuthDonorFamily::Xbox360,
        },
        1,
        true,
        false,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::XboxOneConsole,
        PlatformWireProtocol::XgipXboxOneSeries,
        PlatformImplementationStatus::Planned,
        {
            AuthRequirementKind::LiveOfficialDonorPassthrough,
            AuthDonorFamily::XboxOneSeries,
        },
        1,
        true,
        true,
        false,
        false,
        false
    ),
    makeProfile(
        PlatformId::XboxSeriesConsole,
        PlatformWireProtocol::XgipXboxOneSeries,
        PlatformImplementationStatus::Planned,
        {
            AuthRequirementKind::LiveOfficialDonorPassthrough,
            AuthDonorFamily::XboxOneSeries,
        },
        1,
        true,
        true,
        false,
        false,
        false
    ),
}};

constexpr std::size_t indexOf(PlatformId id) {
    return static_cast<std::size_t>(id);
}

} // namespace

const PlatformProfile& PlatformProfileRegistry::profile(PlatformId id) {
    const std::size_t index = indexOf(id);

    if (index >= kProfiles.size()) {
        return kProfiles[0];
    }

    return kProfiles[index];
}

const PlatformProfile& PlatformProfileRegistry::defaultProfile() {
    return kProfiles[0];
}

const char* PlatformProfileRegistry::platformName(PlatformId id) {
    switch (id) {
        case PlatformId::PcXinput360: return "PC_XINPUT_360";
        case PlatformId::PcGenericHid: return "PC_GENERIC_HID";
        case PlatformId::AndroidGamepad: return "ANDROID_GAMEPAD";
        case PlatformId::IosGameController: return "IOS_GAME_CONTROLLER";
        case PlatformId::NintendoSwitch: return "NINTENDO_SWITCH";
        case PlatformId::Playstation3: return "PLAYSTATION_3";
        case PlatformId::Playstation4: return "PLAYSTATION_4";
        case PlatformId::Playstation5: return "PLAYSTATION_5";
        case PlatformId::Xbox360Console: return "XBOX_360_CONSOLE";
        case PlatformId::XboxOneConsole: return "XBOX_ONE_CONSOLE";
        case PlatformId::XboxSeriesConsole: return "XBOX_SERIES_CONSOLE";
        default: return "UNKNOWN";
    }
}

const char* PlatformProfileRegistry::protocolName(
    PlatformWireProtocol protocol
) {
    switch (protocol) {
        case PlatformWireProtocol::XusbXbox360:
            return "XUSB_XBOX360";
        case PlatformWireProtocol::GenericHid:
            return "GENERIC_HID";
        case PlatformWireProtocol::NintendoSwitchHid:
            return "NINTENDO_SWITCH_HID";
        case PlatformWireProtocol::Playstation3Hid:
            return "PLAYSTATION3_HID";
        case PlatformWireProtocol::Playstation4Hid:
            return "PLAYSTATION4_HID";
        case PlatformWireProtocol::Playstation5Hid:
            return "PLAYSTATION5_HID";
        case PlatformWireProtocol::Xbox360Console:
            return "XBOX360_CONSOLE";
        case PlatformWireProtocol::XgipXboxOneSeries:
            return "XGIP_XBOX_ONE_SERIES";
        case PlatformWireProtocol::MobileHid:
            return "MOBILE_HID";
        default:
            return "UNKNOWN";
    }
}

} // namespace oag
