#include "oag/output/platform/platform_activation_gate.h"

namespace oag {

PlatformActivationDecision PlatformActivationGate::evaluate(
    const PlatformProfile& profile,
    const IPlatformAuthProvider* authProvider
) const {
    if (
        profile.implementation !=
        PlatformImplementationStatus::RuntimeAvailable
    ) {
        return {
            PlatformActivationStatus::NotImplemented,
        };
    }

    if (!profile.auth.required()) {
        return {
            PlatformActivationStatus::Ready,
        };
    }

    if (authProvider == nullptr ||
        !authProvider->canSatisfy(profile.auth)) {
        return {
            PlatformActivationStatus::AuthProviderMissing,
        };
    }

    const AuthProviderStatus status = authProvider->status();

    if (
        status != AuthProviderStatus::Ready &&
        status != AuthProviderStatus::InProgress
    ) {
        return {
            PlatformActivationStatus::AuthProviderNotReady,
        };
    }

    return {
        PlatformActivationStatus::Ready,
    };
}

} // namespace oag
