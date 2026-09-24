#pragma once

#include <cstdint>

#include "oag/auth/platform_auth_provider.h"
#include "oag/output/platform/platform_profile.h"

namespace oag {

enum class PlatformActivationStatus : std::uint8_t {
    Ready = 0,
    NotImplemented,
    AuthProviderMissing,
    AuthProviderNotReady,
};

struct PlatformActivationDecision {
    PlatformActivationStatus status =
        PlatformActivationStatus::NotImplemented;

    constexpr bool ready() const {
        return status == PlatformActivationStatus::Ready;
    }
};

class PlatformActivationGate {
public:
    PlatformActivationDecision evaluate(
        const PlatformProfile& profile,
        const IPlatformAuthProvider* authProvider
    ) const;
};

} // namespace oag
