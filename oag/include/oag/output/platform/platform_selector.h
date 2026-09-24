#pragma once

#include <cstdint>

#include "oag/output/platform/platform_profile.h"

namespace oag {

enum class PlatformSelectionMode : std::uint8_t {
    Fixed = 0,
    Auto,
};

enum class PlatformSelectionSource : std::uint8_t {
    DefaultFallback = 0,
    PersistedPreference,
    BootOverride,
    HostFingerprint,
};

struct PlatformSelectionRequest {
    PlatformSelectionMode mode = PlatformSelectionMode::Auto;
    PlatformId preferred = PlatformId::PcXinput360;

    bool hasPersistedPreference = false;
    PlatformId persistedPreference = PlatformId::PcXinput360;

    bool hasBootOverride = false;
    PlatformId bootOverride = PlatformId::PcXinput360;

    bool hasHostFingerprint = false;
    PlatformId hostFingerprint = PlatformId::PcXinput360;
};

struct PlatformSelectionResult {
    PlatformId platform = PlatformId::PcXinput360;
    PlatformSelectionSource source =
        PlatformSelectionSource::DefaultFallback;
};

class PlatformSelector {
public:
    PlatformSelectionResult select(
        const PlatformSelectionRequest& request
    ) const;
};

} // namespace oag
