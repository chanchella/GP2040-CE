#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compatibility hook required by the pinned OpenStickCommunity
// Pico-PIO-USB host implementation used by the G2E3 Golden baseline.
// Zero means use each endpoint's native polling interval.
extern volatile uint8_t interval_override;

#ifdef __cplusplus
}
#endif
