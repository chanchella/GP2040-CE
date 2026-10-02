#pragma once
// RP2350 A2: preserve the reserved final sector plus two Bluetooth sectors.
#define PICO_FLASH_BANK_STORAGE_OFFSET (4u * 1024u * 1024u - 3u * 4096u)
