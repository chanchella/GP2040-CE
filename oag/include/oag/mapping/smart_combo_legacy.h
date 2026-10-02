#pragma once
#include "oag/config/smart_combo_config.h"
#include "oag/config/diamond_persistent_config.h"
namespace oag {
std::uint16_t oagSmartLegacyMask(const OagSmartGame&, const std::array<DiamondComboProgram, kDiamondComboSlots>&);
} // namespace oag
