#pragma once

#include "oag/output/platform/platform_profile.h"

namespace oag {

class PlatformProfileRegistry {
public:
    static const PlatformProfile& profile(PlatformId id);
    static const PlatformProfile& defaultProfile();

    static const char* platformName(PlatformId id);
    static const char* protocolName(PlatformWireProtocol protocol);
};

} // namespace oag
