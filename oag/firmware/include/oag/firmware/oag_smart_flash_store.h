#pragma once
#include "oag/config/oag_smart_store.h"
namespace oag::firmware {
class OagSmartFlashStorage final : public oag::OagSmartStorage {
public:
    const oag::OagSmartRecord* record(std::size_t game,std::uint8_t copy) const override;
    bool write(std::size_t game,std::uint8_t copy,const std::uint8_t* bytes,std::size_t size) override;
    bool ready() const override;
};
} // namespace oag::firmware
