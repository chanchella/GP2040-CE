#pragma once

#include <cstdint>

namespace oag::firmware {

class UsbPioHost {
public:
    bool start();
    void task();
    void stop();

    std::uint8_t physicalRootMask() const;

    void reconcileRootEvents(
        std::uint8_t removeMask,
        std::uint8_t attachMask
    );

    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};

} // namespace oag::firmware
