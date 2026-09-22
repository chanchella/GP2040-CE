#ifndef _UNIVERSAL_XINPUT_DEVICE_H_
#define _UNIVERSAL_XINPUT_DEVICE_H_

#include <stdint.h>

#include "gpdriver.h"
#include "drivers/xinput/XInputDescriptors.h"

static constexpr uint8_t UNIVERSAL_XINPUT_DEVICE_COUNT = 4;
static constexpr uint8_t UNIVERSAL_XINPUT_OUT_SIZE = 32;

class UniversalXInputDevice {
public:
    UniversalXInputDevice(UniversalXInputDevice const&) = delete;
    void operator=(UniversalXInputDevice const&) = delete;

    static UniversalXInputDevice& getInstance();

    void initializeClassDriver(usbd_class_driver_t& driver);
    bool process();

    const uint8_t* configurationDescriptor() const;

private:
    UniversalXInputDevice();

    static void classInit();
    static void classReset(uint8_t rhport);
    static uint16_t classOpen(
        uint8_t rhport,
        tusb_desc_interface_t const* itfDescriptor,
        uint16_t maxLength
    );
    static bool classControlXfer(
        uint8_t rhport,
        uint8_t stage,
        tusb_control_request_t const* request
    );
    static bool classXfer(
        uint8_t rhport,
        uint8_t epAddr,
        xfer_result_t result,
        uint32_t xferredBytes
    );

    void reset();
    uint16_t open(
        uint8_t rhport,
        tusb_desc_interface_t const* itfDescriptor,
        uint16_t maxLength
    );
    bool xfer(
        uint8_t rhport,
        uint8_t epAddr,
        xfer_result_t result,
        uint32_t xferredBytes
    );

    static XInputReport makeNeutralReport();
    static XInputReport makeReport(GamepadState const& state);

    int8_t slotForOutEndpoint(uint8_t epAddr) const;

    uint8_t endpointIn[UNIVERSAL_XINPUT_DEVICE_COUNT] {};
    uint8_t endpointOut[UNIVERSAL_XINPUT_DEVICE_COUNT] {};

    XInputReport txReport[UNIVERSAL_XINPUT_DEVICE_COUNT] {};
    XInputReport lastReport[UNIVERSAL_XINPUT_DEVICE_COUNT] {};
    bool lastReportValid[UNIVERSAL_XINPUT_DEVICE_COUNT] {};

    uint8_t outTransferBuffer[UNIVERSAL_XINPUT_DEVICE_COUNT][UNIVERSAL_XINPUT_OUT_SIZE] {};
    uint8_t lastOutReport[UNIVERSAL_XINPUT_DEVICE_COUNT][UNIVERSAL_XINPUT_OUT_SIZE] {};
    bool outReportPending[UNIVERSAL_XINPUT_DEVICE_COUNT] {};
};

#define UXINPUT_DEVICE UniversalXInputDevice::getInstance()

#endif
