#ifndef _UNIVERSAL_BLUETOOTH_HOST_H_
#define _UNIVERSAL_BLUETOOTH_HOST_H_

#include "gpaddon.h"

#ifndef OAG_BLUETOOTH_HOST_ENABLED
#define OAG_BLUETOOTH_HOST_ENABLED 0
#endif

class UniversalBluetoothHostAddon : public GPAddon {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override {}
    void postprocess(bool sent) override { (void)sent; }
    void reinit() override {}
    std::string name() override { return "UniversalBluetoothHost"; }

private:
    bool initialized = false;
};

#endif
