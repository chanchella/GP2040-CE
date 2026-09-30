#pragma once

#include <cstddef>

namespace oag::firmware {

class DiamondConfigStore;

class DiamondWifiPortal {
public:
    bool start(DiamondConfigStore& store);
    bool started() const { return started_; }

    void handleHttpRequest(
        void* client,
        const char* request,
        std::size_t requestLength
    );

private:
    DiamondConfigStore* store_ = nullptr;
    bool started_ = false;
};

} // namespace oag::firmware
