#pragma once

#include <cstddef>
#include <cstdint>

namespace oag::firmware {

class DiamondConfigStore;

class DiamondWifiPortal {
public:
    bool start(DiamondConfigStore& store);
    void task();
    bool started() const { return started_; }

    void handleHttpRequest(
        void* client,
        const char* request,
        std::size_t requestLength
    );

private:
    bool authorized(const char* request) const;
    bool csrfAuthorized(const char* request, const char* body) const;
    void createSession();
    void clearSession();
    void scheduleReboot(std::uint32_t delayMs);

    DiamondConfigStore* store_ = nullptr;
    char sessionToken_[33] {};
    char csrfToken_[33] {};
    std::uint64_t sessionExpiresUs_ = 0;
    std::uint64_t loginBlockedUntilUs_ = 0;
    std::uint64_t rebootAtUs_ = 0;
    std::uint8_t failedLoginCount_ = 0;
    bool sessionActive_ = false;
    bool rebootPending_ = false;
    bool started_ = false;
};

} // namespace oag::firmware
