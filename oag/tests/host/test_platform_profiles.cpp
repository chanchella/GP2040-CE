#include <cassert>
#include <cstring>

#include "oag/output/platform/platform_profile_registry.h"

using namespace oag;

int main() {
    const PlatformProfile& pc =
        PlatformProfileRegistry::defaultProfile();

    assert(pc.id == PlatformId::PcXinput360);
    assert(pc.wireProtocol == PlatformWireProtocol::XusbXbox360);
    assert(
        pc.implementation ==
        PlatformImplementationStatus::RuntimeAvailable
    );
    assert(!pc.auth.required());
    assert(pc.supportsRumble);
    assert(pc.hardwareVerified);
    assert(std::strcmp(
        PlatformProfileRegistry::platformName(pc.id),
        "PC_XINPUT_360"
    ) == 0);

    const PlatformProfile& series =
        PlatformProfileRegistry::profile(
            PlatformId::XboxSeriesConsole
        );

    assert(
        series.wireProtocol ==
        PlatformWireProtocol::XgipXboxOneSeries
    );
    assert(series.auth.required());
    assert(
        series.auth.kind ==
        AuthRequirementKind::LiveOfficialDonorPassthrough
    );
    assert(
        series.auth.donorFamily ==
        AuthDonorFamily::XboxOneSeries
    );
    assert(!series.hardwareVerified);

    const PlatformProfile& ps4 =
        PlatformProfileRegistry::profile(
            PlatformId::Playstation4
        );

    assert(ps4.auth.required());
    assert(ps4.auth.donorFamily == AuthDonorFamily::DualShock4);

    const PlatformProfile& switchProfile =
        PlatformProfileRegistry::profile(
            PlatformId::NintendoSwitch
        );

    assert(!switchProfile.auth.required());
    assert(
        switchProfile.implementation ==
        PlatformImplementationStatus::Planned
    );

    return 0;
}
