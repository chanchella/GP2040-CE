#ifndef _UNIVERSAL_OUTPUT_ROUTER_H_
#define _UNIVERSAL_OUTPUT_ROUTER_H_

#include "gpaddon.h"

#ifndef UNIVERSAL_OUTPUT_ROUTER_ENABLED
#define UNIVERSAL_OUTPUT_ROUTER_ENABLED 0
#endif

class UniversalOutputRouterAddon : public GPAddon {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override {}
    void postprocess(bool sent) override {}
    void reinit() override {}
    std::string name() override { return "UniversalOutputRouter"; }
};

#endif
