#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oag {

struct KeyboardState {
    static constexpr std::size_t kUsageCount = 256;
    static constexpr std::size_t kWordCount = kUsageCount / 64;

    bool connected = false;
    std::array<std::uint64_t, kWordCount> usages {};
    std::uint8_t modifiers = 0;
    std::uint32_t generation = 0;
    std::uint64_t timestampUs = 0;

    constexpr bool pressed(std::uint8_t usage) const {
        const std::size_t word = usage / 64u;
        const std::size_t bit = usage % 64u;
        return (usages[word] & (std::uint64_t{1} << bit)) != 0;
    }

    constexpr void setPressed(std::uint8_t usage, bool down) {
        const std::size_t word = usage / 64u;
        const std::size_t bit = usage % 64u;
        const std::uint64_t mask = std::uint64_t{1} << bit;

        if (down) {
            usages[word] |= mask;
        } else {
            usages[word] &= ~mask;
        }
    }
};

} // namespace oag
