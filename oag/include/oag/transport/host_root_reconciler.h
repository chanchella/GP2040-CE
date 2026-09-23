#pragma once

#include <cstdint>

namespace oag {

struct HostRootReconcilePlan {
    std::uint8_t removeMask = 0;
    std::uint8_t attachMask = 0;

    constexpr bool healthy() const {
        return removeMask == 0 && attachMask == 0;
    }
};

constexpr HostRootReconcilePlan planHostRootReconcile(
    std::uint8_t physicalMask,
    std::uint8_t mountedMask,
    std::uint8_t validMask = 0x07
) {
    physicalMask &= validMask;
    mountedMask &= validMask;

    return {
        static_cast<std::uint8_t>(mountedMask & ~physicalMask),
        static_cast<std::uint8_t>(physicalMask & ~mountedMask),
    };
}

} // namespace oag
