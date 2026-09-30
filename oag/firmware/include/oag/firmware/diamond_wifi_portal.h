#pragma once

#include <cstddef>
#include <cstdint>

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
    bool authorized(const char* request) const;
    void createSession();
    void clearSession();

    DiamondConfigStore* store_ = nullptr;
    char sessionToken_[33] {};
    std::uint64_t sessionExpiresUs_ = 0;
    std::uint64_t loginBlockedUntilUs_ = 0;
    std::uint8_t failedLoginCount_ = 0;
    bool sessionActive_ = false;
    bool started_ = false;
};

} // namespace oag::firmware
