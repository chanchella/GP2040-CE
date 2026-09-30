#pragma once

namespace oag::firmware {

class DiamondWifiPortal {
public:
    bool start();
    bool started() const { return started_; }

private:
    bool started_ = false;
};

} // namespace oag::firmware
