#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t oag_pio_physical_root_mask(void);
void oag_pio_force_reenumerate_connected_roots(void);

#ifdef __cplusplus
}
#endif
