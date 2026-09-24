#pragma once

#include <cstdint>

#include "tusb.h"
#include "host/usbh.h"

#ifdef __cplusplus
extern "C" {
#endif

enum : std::uint8_t {
    OAG_XINPUT_UNKNOWN = 0,
    OAG_XINPUT_XBOX360 = 1,
};

std::uint8_t tuh_xinput_instance_count(std::uint8_t dev_addr);
bool tuh_xinput_mounted(std::uint8_t dev_addr, std::uint8_t instance);
bool tuh_xinput_ready(std::uint8_t dev_addr, std::uint8_t instance);
bool tuh_xinput_receive_report(std::uint8_t dev_addr, std::uint8_t instance);
bool tuh_xinput_send_report(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
);

void tuh_xinput_mount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t type,
    std::uint8_t subtype
);

void tuh_xinput_umount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance
);

void tuh_xinput_report_received_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
);

void tuh_xinput_report_sent_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
);

bool xinputh_init(void);
bool xinputh_open(
    std::uint8_t rhport,
    std::uint8_t dev_addr,
    tusb_desc_interface_t const* desc_itf,
    std::uint16_t max_len
);
bool xinputh_set_config(std::uint8_t dev_addr, std::uint8_t itf_num);
bool xinputh_xfer_cb(
    std::uint8_t dev_addr,
    std::uint8_t ep_addr,
    xfer_result_t result,
    std::uint32_t xferred_bytes
);
void xinputh_close(std::uint8_t dev_addr);

#ifdef __cplusplus
}
#endif
