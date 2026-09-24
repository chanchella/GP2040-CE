#pragma once

#include <cstdint>

#include "oag/auth/auth_requirement.h"
#include "oag/feedback/rumble_command.h"
#include "oag/output/logical_gamepad_state.h"
#include "oag/output/platform/platform_profile.h"

namespace oag {

struct PlatformOutputCapabilities {
    bool rumble = false;
    bool triggerRumble = false;
    bool motion = false;
    bool touch = false;
};

class IPlatformOutputDriver {
public:
    virtual ~IPlatformOutputDriver() = default;

    virtual PlatformId platformId() const = 0;
    virtual PlatformOutputCapabilities capabilities() const = 0;
    virtual AuthRequirement authRequirement() const = 0;

    virtual bool initialize() = 0;
    virtual void poll() = 0;

    virtual bool submit(
        std::uint8_t logicalSlot,
        const LogicalGamepadState& state
    ) = 0;

    // Returns the newest platform-originated rumble command and the logical
    // output slot that received it. This keeps reverse feedback aligned with
    // the same physical controller that produced that logical controller.
    // Platform-specific auxiliary feedback (lightbar, LEDs, trigger haptics,
    // etc.) remains in dedicated capability channels.
    virtual bool takeRumble(
        std::uint8_t& logicalSlot,
        RumbleCommand& output
    ) = 0;
};

} // namespace oag
