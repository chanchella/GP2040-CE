#pragma once
#include <cstdint>
#include <cstddef>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
extern "C" std::uint8_t oagTestFlash[4u * 1024u * 1024u];
#define XIP_BASE (reinterpret_cast<std::uintptr_t>(oagTestFlash))
void flash_range_erase(std::uint32_t, std::size_t);
void flash_range_program(std::uint32_t, const std::uint8_t*, std::size_t);
