#pragma once
#include <cstdint>
#define PICO_OK 0
int flash_safe_execute(void (*)(void*), void*, std::uint32_t);
