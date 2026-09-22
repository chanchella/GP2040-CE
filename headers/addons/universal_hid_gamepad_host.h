#ifndef _UNIVERSAL_HID_GAMEPAD_HOST_H_
#define _UNIVERSAL_HID_GAMEPAD_HOST_H_

#include "gpaddon.h"
#include "usblistener.h"
#include "input/universal_input_manager.h"

#ifndef UNIVERSAL_HID_GAMEPAD_HOST_ENABLED
#define UNIVERSAL_HID_GAMEPAD_HOST_ENABLED 0
#endif

class UniversalHIDGamepadHostAddon : public GPAddon, public USBListener {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override {}
    void postprocess(bool sent) override {}
    void reinit() override {}
    std::string name() override { return "UniversalHIDGamepadHost"; }
    USBListener* getListener() override { return this; }

    void mount(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) override;
    void xmount(uint8_t dev_addr, uint8_t instance, uint8_t controllerType, uint8_t subtype) override {}
    void unmount(uint8_t dev_addr) override;
    void report_received(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) override;
    void report_sent(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) override {}
    void set_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) override {}
    void get_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) override {}

private:
    static constexpr uint8_t MAX_INTERFACES = 8;
    static constexpr uint16_t MAX_DESCRIPTOR_BYTES = 512;
    static constexpr uint8_t MAX_FIELDS = 96;
    static constexpr uint8_t MAX_LOCAL_USAGES = 32;
    static constexpr uint8_t MAX_REPORT_BYTES = 64;
    static constexpr uint8_t GLOBAL_STACK_DEPTH = 4;

    struct HidField {
        bool used = false;
        uint8_t reportId = 0;
        uint16_t bitOffset = 0;
        uint8_t bitSize = 0;
        uint16_t usagePage = 0;
        uint16_t usage = 0;
        int32_t logicalMin = 0;
        int32_t logicalMax = 0;
    };

    struct InterfaceState {
        bool active = false;
        uint8_t devAddr = 0;
        uint8_t instance = 0;
        uint8_t protocol = 0;
        uint16_t vid = 0;
        uint16_t pid = 0;

        bool descriptorPending = false;
        uint16_t descriptorLength = 0;
        uint8_t descriptor[MAX_DESCRIPTOR_BYTES] {};

        bool parsed = false;
        bool isGamepad = false;
        uint16_t topUsagePage = 0;
        uint16_t topUsage = 0;

        uint8_t fieldCount = 0;
        HidField fields[MAX_FIELDS] {};

        bool reportPending = false;
        uint8_t reportLength = 0;
        uint8_t report[MAX_REPORT_BYTES] {};

        uint8_t globalSlot = UNIVERSAL_INPUT_SLOT_INVALID;
        UniversalDeviceMatch device {};
        GamepadState lastState {};
        bool lastStateValid = false;
    };

    InterfaceState interfaces[MAX_INTERFACES] {};

    InterfaceState* findInterface(uint8_t devAddr, uint8_t instance);
    InterfaceState* allocateInterface();

    void resetInterface(InterfaceState& state);
    void parseDescriptor(InterfaceState& state);
    void processReport(InterfaceState& state);

    static uint32_t extractBits(
        uint8_t const* data,
        uint16_t dataLength,
        uint16_t bitOffset,
        uint8_t bitSize
    );

    static uint16_t scaleAxis(
        int32_t value,
        int32_t logicalMin,
        int32_t logicalMax
    );

    static uint8_t scaleTrigger(
        int32_t value,
        int32_t logicalMin,
        int32_t logicalMax
    );

    static void applyButton(
        GamepadState& state,
        uint16_t usage,
        bool pressed
    );

    static uint8_t hatToDpad(
        int32_t value,
        int32_t logicalMin,
        int32_t logicalMax
    );
};

#endif
