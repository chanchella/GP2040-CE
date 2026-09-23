#pragma once

#include <cstdint>

namespace oag {

struct DeviceId {
    static constexpr std::uint16_t kInvalidIndex = 0xFFFFu;

    std::uint16_t index = kInvalidIndex;
    std::uint16_t generation = 0;

    constexpr bool valid() const {
        return index != kInvalidIndex && generation != 0;
    }

    friend constexpr bool operator==(DeviceId lhs, DeviceId rhs) {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend constexpr bool operator!=(DeviceId lhs, DeviceId rhs) {
        return !(lhs == rhs);
    }
};

} // namespace oag
