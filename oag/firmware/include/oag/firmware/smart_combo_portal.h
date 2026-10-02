#pragma once
#include "oag/firmware/smart_combo_store.h"
#include "oag/mapping/smart_combo_engine.h"
namespace oag::firmware {
class OagSmartComboPortal {
public:
    using Reply = void (*)(void*, const char*, const char*, const char*);
    void attach(OagSmartComboStore& store, OagSmartComboEngine& engine) { store_ = &store; test_ = &engine; }
    bool handle(void* client, const char* method, const char* path, const char* body, std::uint64_t now, Reply);
private:
    OagSmartComboStore* store_ = nullptr;
    OagSmartProgram pending_ {};
    OagSmartComboEngine* test_ = nullptr;
    std::uint32_t token_ = 0, pendingRevision_ = 0, testRevision_ = 0;
    std::uint64_t expiresUs_ = 0;
    std::size_t pendingGame_ = 0, pendingSlot_ = 0;
    std::uint8_t received_ = 0;
    bool transaction_ = false, testing_ = false;
    std::uint32_t testTime_ = 0;
    std::array<char, 1800> replyBuffer_ {};
    std::array<char, sizeof(OagSmartBranch) * 2 + 1> hexBuffer_ {};
};
} // namespace oag::firmware
