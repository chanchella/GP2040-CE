#ifndef _UNIVERSAL_XINPUT_HOST_H_
#define _UNIVERSAL_XINPUT_HOST_H_

#include "gpaddon.h"
#include "usblistener.h"
#include "gamepad.h"
#include "input/universal_input_manager.h"

#ifndef UNIVERSAL_XINPUT_HOST_ENABLED
#define UNIVERSAL_XINPUT_HOST_ENABLED 0
#endif

class UniversalXInputHostAddon : public GPAddon, public USBListener {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override {}
    void postprocess(bool sent) override {}
    void reinit() override {}
    std::string name() override { return "UniversalXInputHost"; }
    USBListener* getListener() override { return this; }

    void mount(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) override {}
    void xmount(uint8_t dev_addr, uint8_t instance, uint8_t controllerType, uint8_t subtype) override;
    void unmount(uint8_t dev_addr) override;
    void report_received(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) override;
    void report_sent(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) override;
    void set_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) override {}
    void get_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) override {}

private:
    static constexpr uint8_t USB_SLOT_COUNT = 3;

    enum class XgipInitPhase : uint8_t {
        NONE = 0,
        POWER,
        SYSTEM_INIT,
        EXTRA_INPUT,
        LED,
        AUTH_DONE,
        READY,
    };

    struct XInputTransportSlot {
        bool mounted = false;
        uint8_t devAddr = 0;
        uint8_t instance = 0;
        uint8_t controllerType = 0;
        uint8_t subtype = 0;
        uint8_t globalSlot = UNIVERSAL_INPUT_SLOT_INVALID;
        UniversalInputSource source = UniversalInputSource::NONE;
        UniversalDeviceMatch device {};
        GamepadState state {};

        XgipInitPhase xgipPhase = XgipInitPhase::NONE;
        bool xgipTxPending = false;

        uint32_t feedbackGeneration = 0;
        uint8_t xgipRumbleSequence = 1;
    };

    XInputTransportSlot slots[USB_SLOT_COUNT];

    void resetSlot(uint8_t slot);
    int8_t findSlot(uint8_t devAddr, uint8_t instance) const;

    int8_t allocateSlot(
        uint8_t devAddr,
        uint8_t instance,
        uint8_t controllerType,
        uint8_t subtype,
        UniversalInputSource source,
        UniversalDeviceMatch const& match
    );

    void restartXgipInit(uint8_t localSlot);
    void serviceXgipInit(uint8_t localSlot);
    void advanceXgipInit(uint8_t localSlot);
    void serviceRumble(uint8_t localSlot);
};

#endif
