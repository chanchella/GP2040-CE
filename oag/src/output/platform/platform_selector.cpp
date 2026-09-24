#include "oag/output/platform/platform_selector.h"

namespace oag {

PlatformSelectionResult PlatformSelector::select(
    const PlatformSelectionRequest& request
) const {
    if (request.hasBootOverride) {
        return {
            request.bootOverride,
            PlatformSelectionSource::BootOverride,
        };
    }

    if (request.mode == PlatformSelectionMode::Fixed) {
        return {
            request.preferred,
            PlatformSelectionSource::PersistedPreference,
        };
    }

    if (request.hasHostFingerprint) {
        return {
            request.hostFingerprint,
            PlatformSelectionSource::HostFingerprint,
        };
    }

    if (request.hasPersistedPreference) {
        return {
            request.persistedPreference,
            PlatformSelectionSource::PersistedPreference,
        };
    }

    return {
        PlatformId::PcXinput360,
        PlatformSelectionSource::DefaultFallback,
    };
}

} // namespace oag
