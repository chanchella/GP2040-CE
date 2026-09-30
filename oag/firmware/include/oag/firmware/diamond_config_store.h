#pragma once

#include <cstdint>

#include "oag/config/diamond_config_record.h"

namespace oag::firmware {

class DiamondConfigStore {
public:
    bool load();
    bool save();

    oag::DiamondPersistentConfig& config() { return config_; }
    const oag::DiamondPersistentConfig& config() const { return config_; }

    bool loadedFromFlash() const { return loadedFromFlash_; }
    std::uint32_t generation() const { return generation_; }

private:
    oag::DiamondPersistentConfig config_ {};
    oag::DiamondConfigRecord pendingRecord_ {};
    std::uint32_t generation_ = 0;
    std::uint8_t activeSlot_ = 0xFF;
    bool loadedFromFlash_ = false;
};

} // namespace oag::firmware
