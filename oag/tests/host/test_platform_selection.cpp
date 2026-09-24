#include <cassert>
#include <cstddef>
#include <cstdint>

#include "oag/auth/platform_auth_provider.h"
#include "oag/output/platform/platform_activation_gate.h"
#include "oag/output/platform/platform_profile_registry.h"
#include "oag/output/platform/platform_selector.h"

using namespace oag;

namespace {

class FakeAuthProvider final : public IPlatformAuthProvider {
public:
    bool satisfy = false;
    AuthProviderStatus providerStatus =
        AuthProviderStatus::WaitingForDonor;

    bool canSatisfy(
        const AuthRequirement&
    ) const override {
        return satisfy;
    }

    bool bindDonor(DeviceId) override {
        return false;
    }

    void releaseDonor() override {}

    AuthProviderStatus status() const override {
        return providerStatus;
    }

    void poll() override {}

    bool handleChallenge(
        const std::uint8_t*,
        std::size_t,
        std::uint8_t*,
        std::size_t,
        std::size_t&
    ) override {
        return false;
    }
};

} // namespace

int main() {
    PlatformSelector selector;

    PlatformSelectionRequest request {};
    PlatformSelectionResult result = selector.select(request);

    assert(result.platform == PlatformId::PcXinput360);
    assert(
        result.source ==
        PlatformSelectionSource::DefaultFallback
    );

    request.hasPersistedPreference = true;
    request.persistedPreference = PlatformId::NintendoSwitch;

    result = selector.select(request);
    assert(result.platform == PlatformId::NintendoSwitch);
    assert(
        result.source ==
        PlatformSelectionSource::PersistedPreference
    );

    request.hasHostFingerprint = true;
    request.hostFingerprint = PlatformId::Playstation4;

    result = selector.select(request);
    assert(result.platform == PlatformId::Playstation4);
    assert(
        result.source ==
        PlatformSelectionSource::HostFingerprint
    );

    request.hasBootOverride = true;
    request.bootOverride = PlatformId::XboxSeriesConsole;

    result = selector.select(request);
    assert(result.platform == PlatformId::XboxSeriesConsole);
    assert(
        result.source ==
        PlatformSelectionSource::BootOverride
    );

    PlatformActivationGate gate;

    const PlatformProfile& pc =
        PlatformProfileRegistry::profile(
            PlatformId::PcXinput360
        );

    assert(gate.evaluate(pc, nullptr).ready());

    // SoftwareFoundation must not be activated as if it were complete.
    const PlatformProfile& switchProfile =
        PlatformProfileRegistry::profile(
            PlatformId::NintendoSwitch
        );

    assert(
        gate.evaluate(switchProfile, nullptr).status ==
        PlatformActivationStatus::NotImplemented
    );

    // PS4 is also SoftwareFoundation today, so implementation status blocks
    // it before auth readiness is even considered.
    const PlatformProfile& ps4 =
        PlatformProfileRegistry::profile(
            PlatformId::Playstation4
        );

    FakeAuthProvider auth;
    auth.satisfy = true;
    auth.providerStatus = AuthProviderStatus::Ready;

    assert(
        gate.evaluate(ps4, &auth).status ==
        PlatformActivationStatus::NotImplemented
    );

    return 0;
}
