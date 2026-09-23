#ifndef _UNIVERSAL_HID_HUMAN_INTERFACE_HOST_H_
#define _UNIVERSAL_HID_HUMAN_INTERFACE_HOST_H_

#include "gpaddon.h"
#include "usblistener.h"
#include "input/universal_human_interface_manager.h"

#ifndef UNIVERSAL_HID_HUMAN_INTERFACE_HOST_ENABLED
#define UNIVERSAL_HID_HUMAN_INTERFACE_HOST_ENABLED 0
#endif

class UniversalHIDHumanInterfaceHostAddon
    : public GPAddon,
      public USBListener {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override {}
    void postprocess(bool sent) override { (void)sent; }
    void reinit() override {}
    std::string name() override {
        return "UniversalHIDHumanInterfaceHost";
    }

    USBListener* getListener() override { return this; }

    void mount(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t const* desc_report,
        uint16_t desc_len
    ) override;

    void xmount(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t controllerType,
        uint8_t subtype
    ) override {
        (void)dev_addr;
        (void)instance;
        (void)controllerType;
        (void)subtype;
    }

    void unmount(uint8_t dev_addr) override;

    void report_received(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t const* report,
        uint16_t len
    ) override;

    void report_sent(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t const* report,
        uint16_t len
    ) override {
        (void)dev_addr;
        (void)instance;
        (void)report;
        (void)len;
    }

    void set_report_complete(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t report_id,
        uint8_t report_type,
        uint16_t len
    ) override;

    void get_report_complete(
        uint8_t dev_addr,
        uint8_t instance,
        uint8_t report_id,
        uint8_t report_type,
        uint16_t len
    ) override {
        (void)dev_addr;
        (void)instance;
        (void)report_id;
        (void)report_type;
        (void)len;
    }

private:
    static constexpr uint8_t MAX_INTERFACES = 8;
    static constexpr uint16_t MAX_DESCRIPTOR_BYTES = 512;
    static constexpr uint16_t MAX_FIELDS = 272;
    static constexpr uint8_t MAX_LOCAL_USAGES = 64;
    static constexpr uint8_t MAX_REPORT_BYTES = 64;
    static constexpr uint8_t GLOBAL_STACK_DEPTH = 4;
    static constexpr uint8_t MAX_REPORT_CONTRIBUTIONS = 8;

    enum class FieldTarget : uint8_t {
        NONE = 0,
        KEYBOARD,
        MOUSE,
        CONSUMER,
    };

    struct HidField {
        bool used = false;
        bool isArray = false;
        FieldTarget target = FieldTarget::NONE;

        uint8_t reportId = 0;
        uint16_t bitOffset = 0;
        uint8_t bitSize = 0;

        uint16_t usagePage = 0;
        uint16_t usage = 0;
        uint16_t usageMin = 0;
        uint16_t usageMax = 0;

        int32_t logicalMin = 0;
        int32_t logicalMax = 0;
    };

    struct KeyboardReportContribution {
        bool used = false;
        uint8_t reportId = 0;
        UniversalKeyboardState state {};
    };

    struct MouseReportContribution {
        bool used = false;
        uint8_t reportId = 0;
        uint32_t buttons = 0;
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
        bool isKeyboard = false;
        bool isMouse = false;
        bool hasConsumer = false;
        bool usesReportIds = false;

        uint16_t fieldCount = 0;
        HidField fields[MAX_FIELDS] {};

        bool reportPending = false;
        uint8_t reportLength = 0;
        uint8_t report[MAX_REPORT_BYTES] {};

        uint8_t keyboardSlot = UNIVERSAL_HID_SLOT_INVALID;
        uint8_t mouseSlot = UNIVERSAL_HID_SLOT_INVALID;

        // Standard keyboard lock LEDs. The transfer buffer must remain alive
        // until TinyUSB's asynchronous SET_REPORT completion callback.
        uint8_t ledReportValue = 0;
        uint8_t appliedLedState = 0xFF;
        bool ledTransferPending = false;

        KeyboardReportContribution
            keyboardReports[MAX_REPORT_CONTRIBUTIONS] {};
        MouseReportContribution
            mouseReports[MAX_REPORT_CONTRIBUTIONS] {};
    };

    InterfaceState interfaces[MAX_INTERFACES] {};

    InterfaceState* findInterface(
        uint8_t devAddr,
        uint8_t instance
    );

    InterfaceState* allocateInterface();

    void resetInterface(InterfaceState& state);
    void parseDescriptor(InterfaceState& state);
    void processReport(InterfaceState& state);
    void serviceKeyboardLeds();

    void ensureKeyboardSlot(InterfaceState& state);
    void ensureMouseSlot(InterfaceState& state);

    void publishKeyboardAggregate(
        uint8_t devAddr,
        uint8_t slot
    );

    uint32_t aggregateMouseButtons(
        uint8_t devAddr,
        uint8_t slot
    ) const;

    KeyboardReportContribution*
    findOrCreateKeyboardContribution(
        InterfaceState& state,
        uint8_t reportId
    );

    MouseReportContribution*
    findOrCreateMouseContribution(
        InterfaceState& state,
        uint8_t reportId
    );

    static uint32_t extractBits(
        uint8_t const* data,
        uint16_t dataLength,
        uint16_t bitOffset,
        uint8_t bitSize
    );

    static int32_t signedFieldValue(
        HidField const& field,
        uint32_t raw
    );

    static void appendConsumerUsage(
        UniversalKeyboardState& state,
        uint16_t usage
    );
};

#endif
