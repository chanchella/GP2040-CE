#pragma once

namespace oag::firmware {

class UsbPioHost {
public:
    bool start();
    void task();
    void stop();

    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};

} // namespace oag::firmware
