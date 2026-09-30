#pragma once

#include <cstddef>

#include "oag/config/diamond_persistent_config.h"

namespace oag::firmware {

class DiamondPasswordService {
public:
    static bool provision(
        oag::DiamondSecurityConfig& security,
        const char* username,
        const char* password
    );

    static bool verify(
        const oag::DiamondSecurityConfig& security,
        const char* username,
        const char* password
    );
};

} // namespace oag::firmware
