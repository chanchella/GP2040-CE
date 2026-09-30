#pragma once

#include <cstddef>
#include <cstdint>

namespace oag::firmware {

class DiamondConfigStore;
class DiamondGameLibraryStore;

class DiamondWifiPortal {
public:
    bool start(DiamondConfigStore& store, DiamondGameLibraryStore& games);
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
    DiamondGameLibraryStore* games_ = nullptr;
    std::uint64_t playRebootAtUs_ = 0;
    bool playRebootPending_ = false;
    bool started_ = false;
};

} // namespace oag::firmware
