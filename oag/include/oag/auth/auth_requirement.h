#pragma once

#include <cstdint>

namespace oag {

enum class AuthRequirementKind : std::uint8_t {
    None = 0,
    LiveOfficialDonorPassthrough,
};

enum class AuthDonorFamily : std::uint8_t {
    None = 0,
    Xbox360,
    XboxOneSeries,
    DualShock4,
    DualSense,
};

struct AuthRequirement {
    AuthRequirementKind kind = AuthRequirementKind::None;
    AuthDonorFamily donorFamily = AuthDonorFamily::None;

    constexpr bool required() const {
        return kind != AuthRequirementKind::None;
    }
};

} // namespace oag
