#include "oag/config/diamond_config_record.h"

#include <cstdint>

namespace oag {

std::uint32_t diamondConfigCrc32(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0u - static_cast<std::uint32_t>(crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

void finalizeDiamondConfigRecord(DiamondConfigRecord& record) {
    record.magic = DiamondConfigRecord::kMagic;
    record.recordVersion = DiamondConfigRecord::kRecordVersion;
    record.payloadLength =
        static_cast<std::uint16_t>(sizeof(DiamondPersistentConfig));
    record.payloadCrc32 = diamondConfigCrc32(
        &record.payload,
        sizeof(record.payload)
    );
}

bool validateDiamondConfigRecord(const DiamondConfigRecord& record) {
    if (
        record.magic != DiamondConfigRecord::kMagic ||
        record.recordVersion != DiamondConfigRecord::kRecordVersion ||
        record.payloadLength != sizeof(DiamondPersistentConfig) ||
        record.payload.magic != DiamondPersistentConfig::kMagic ||
        record.payload.schemaVersion != DiamondPersistentConfig::kSchemaVersion ||
        record.payload.runtime.magic != DiamondRuntimeConfig::kMagic ||
        record.payload.runtime.schemaVersion != DiamondRuntimeConfig::kSchemaVersion
    ) {
        return false;
    }

    return record.payloadCrc32 == diamondConfigCrc32(
        &record.payload,
        sizeof(record.payload)
    );
}

const DiamondConfigRecord* selectNewestDiamondConfigRecord(
    const DiamondConfigRecord* slotA,
    const DiamondConfigRecord* slotB
) {
    const bool validA = slotA != nullptr && validateDiamondConfigRecord(*slotA);
    const bool validB = slotB != nullptr && validateDiamondConfigRecord(*slotB);

    if (!validA) {
        return validB ? slotB : nullptr;
    }
    if (!validB) {
        return slotA;
    }

    const auto delta = static_cast<std::int32_t>(
        slotB->generation - slotA->generation
    );
    return delta > 0 ? slotB : slotA;
}

} // namespace oag
