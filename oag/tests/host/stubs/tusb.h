#pragma once
#include <cstdint>
constexpr std::uint8_t HID_PROTOCOL_BOOT=0;
bool tud_hid_n_ready(std::uint8_t);
bool tud_hid_n_report(std::uint8_t,std::uint8_t,const void*,std::uint16_t);
std::uint8_t tud_hid_n_get_protocol(std::uint8_t);
