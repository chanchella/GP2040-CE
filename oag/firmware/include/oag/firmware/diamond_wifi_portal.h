#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/input/gamepad_state.h"

namespace oag::firmware {

class DiamondConfigStore;

class DiamondWifiPortal {
public:
    bool start(
        DiamondConfigStore& store,
        const oag::UniversalGamepadState* liveStates,
        std::size_t liveStateCount
    );
    void task();
    bool started() const { return started_; }

    void handleHttpRequest(
        void* client,
        const char* request,
        std::size_t requestLength
    );

private:
    void schedulePlayReboot(std::uint32_t delayMs);

    DiamondConfigStore* store_ = nullptr;
    const oag::UniversalGamepadState* liveStates_ = nullptr;
    std::size_t liveStateCount_ = 0;
    std::uint64_t playRebootAtUs_ = 0;
    bool playRebootPending_ = false;
    bool started_ = false;
};

} // namespace oag::firmware
