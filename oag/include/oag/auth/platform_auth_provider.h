#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/auth/auth_requirement.h"
#include "oag/device/device_id.h"

namespace oag {

enum class AuthProviderStatus : std::uint8_t {
    Unbound = 0,
    WaitingForDonor,
    Ready,
    InProgress,
    Failed,
};

class IPlatformAuthProvider {
public:
    virtual ~IPlatformAuthProvider() = default;

    virtual bool canSatisfy(
        const AuthRequirement& requirement
    ) const = 0;

    virtual bool bindDonor(DeviceId donor) = 0;
    virtual void releaseDonor() = 0;

    virtual AuthProviderStatus status() const = 0;
    virtual void poll() = 0;

    virtual bool handleChallenge(
        const std::uint8_t* request,
        std::size_t requestLength,
        std::uint8_t* response,
        std::size_t responseCapacity,
        std::size_t& responseLength
    ) = 0;
};

} // namespace oag
