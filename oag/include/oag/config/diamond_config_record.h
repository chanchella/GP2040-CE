#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_persistent_config.h"

namespace oag {

struct DiamondConfigRecord {
    static constexpr std::uint32_t kMagic = 0x4F414743u; // OAGC
    static constexpr std::uint16_t kRecordVersion = 1;

    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t payloadLength = sizeof(DiamondPersistentConfig);
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    DiamondPersistentConfig payload {};
};

static_assert(
    sizeof(DiamondConfigRecord) <= 4096,
    "Diamond config record must fit in one flash sector"
);

std::uint32_t diamondConfigCrc32(const void* data, std::size_t size);
void finalizeDiamondConfigRecord(DiamondConfigRecord& record);
bool validateDiamondConfigRecord(const DiamondConfigRecord& record);
const DiamondConfigRecord* selectNewestDiamondConfigRecord(
    const DiamondConfigRecord* slotA,
    const DiamondConfigRecord* slotB
);

} // namespace oag
