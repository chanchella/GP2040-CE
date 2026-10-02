#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_game_library.h"
#include "oag/mapping/pro_input_processor.h"
#include "oag/config/oag_smart_api.h"

namespace oag::firmware {

class DiamondConfigStore;
class DiamondGameLibraryStore;

class DiamondWifiPortal {
public:
    bool start(DiamondConfigStore& store, DiamondGameLibraryStore& games, bool radioReady=false);
    void attachSmart(oag::OagSmartApi& api) { smart_=&api; }
    void attachProInput(oag::ProInputProcessor& p, const oag::DeviceRegistry& r) { proInput_ = &p; proRegistry_ = &r; }
    void task();
    bool started() const { return started_; }

    void handleHttpRequest(
        void* client,
        const char* request,
        std::size_t requestLength
    );

private:
    bool handleProInputRequest(void*, const char*, const char*, const char*);
    oag::ProInputProcessor* proInput_ = nullptr;
    const oag::DeviceRegistry* proRegistry_ = nullptr;
    oag::DeviceId calibrationDevice_ {};
    std::int64_t calibrationStartX_ = 0;
    void schedulePlayReboot(std::uint32_t delayMs);

    DiamondConfigStore* store_ = nullptr;
    DiamondGameLibraryStore* games_ = nullptr;
    oag::DiamondGameContent scratchGame_ {};
    std::uint64_t playRebootAtUs_ = 0;
    bool playRebootPending_ = false;
    bool started_ = false;
    bool liveMode_ = false;
    oag::OagSmartApi* smart_ = nullptr;
};

} // namespace oag::firmware
