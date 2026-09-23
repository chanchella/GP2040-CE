/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _OAG_MULTI_HID_DRIVER_H_
#define _OAG_MULTI_HID_DRIVER_H_

#include "gpdriver.h"
#include "drivers/oag/OAGMultiHIDDescriptors.h"

class OAGMultiHIDDriver : public GPDriver {
public:
    void initialize() override;
    bool process(Gamepad* gamepad) override;

    void initializeAux() override {}
    void processAux() override {}

    uint16_t get_report(
        uint8_t report_id,
        hid_report_type_t report_type,
        uint8_t* buffer,
        uint16_t reqlen
    ) override;

    void set_report(
        uint8_t report_id,
        hid_report_type_t report_type,
        uint8_t const* buffer,
        uint16_t bufsize
    ) override {}

    void set_report_with_itf(
        uint8_t itf,
        uint8_t report_id,
        hid_report_type_t report_type,
        uint8_t const* buffer,
        uint16_t bufsize
    ) override;

    bool vendor_control_xfer_cb(
        uint8_t rhport,
        uint8_t stage,
        tusb_control_request_t const* request
    ) override;

    const uint16_t* get_descriptor_string_cb(
        uint8_t index,
        uint16_t langid
    ) override;

    const uint8_t* get_descriptor_device_cb() override;
    const uint8_t* get_hid_descriptor_report_cb(uint8_t itf) override;
    const uint8_t* get_descriptor_configuration_cb(uint8_t index) override;
    const uint8_t* get_descriptor_device_qualifier_cb() override;

    uint16_t GetJoystickMidValue() override;
    USBListener* get_usb_auth_listener() override { return nullptr; }

private:
    uint8_t configDescriptor[OAG_MULTI_HID_CONFIG_SIZE] {};
    OAGMultiHIDReport reports[OAG_MULTI_HID_SLOT_COUNT] {};
    OAGMultiHIDReport lastReports[OAG_MULTI_HID_SLOT_COUNT] {};
    bool lastReportValid[OAG_MULTI_HID_SLOT_COUNT] {};

    void buildConfigurationDescriptor();

    static OAGMultiHIDReport buildReport(
        GamepadState const& state
    );

    static uint8_t dpadToHat(uint8_t dpad);
};

#endif
