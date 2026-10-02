#pragma once
#include "oag/config/oag_smart_config.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {
struct OagRecoilTick { std::int32_t xQ16=0, yQ16=0; bool updated=false; };
class OagWeaponTuningEngine {
public:
    void reset();
    OagRecoilTick tick(const OagWeaponSettings&, bool firing, bool ads, bool reload,
                       std::uint64_t nowUs);
    LogicalGamepadState apply(LogicalGamepadState base) const;
    bool active() const { return firing_; }
    static std::uint16_t shotInterval(const OagWeaponSettings& w);
    static std::uint16_t tickInterval(const OagWeaponSettings& w);
private:
    std::uint64_t fireStart_=0, nextTick_=0, reloadUntil_=0;
    std::int32_t xQ16_=0, yQ16_=0, filterX_=0, filterY_=0;
    bool firing_=false, reloading_=false, firstPending_=true;
};
} // namespace oag
