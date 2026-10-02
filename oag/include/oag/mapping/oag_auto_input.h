#pragma once
#include "oag/mapping/pro_input_processor.h"
namespace oag {
// Conservative automatic shaping, isolated from HID parsing and transport setup.
class OagAutoInput {
public:
    static const ProInputSettings& settings(ProInputKind kind);
    MouseMotion mouse(DeviceId device,MouseMotion raw,std::uint64_t nowUs);
    MouseMotion flush(DeviceId device,std::uint64_t nowUs);
    LogicalGamepadState gamepad(DeviceId device,const LogicalGamepadState& shaped);
    void reset();
private:
    struct State {
        DeviceId device {};
        MouseMotion pending {};
        std::uint64_t due = 0;
        std::array<std::int32_t,4> axes {};
        bool padSeen = false;
    };
    State* state(DeviceId device);
    std::array<State,DeviceRegistry::kCapacity> devices_ {};
};
} // namespace oag
