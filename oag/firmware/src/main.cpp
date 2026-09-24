#include <array>
#include <cstdint>
#include <optional>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include "oag/device/device_registry.h"
#include "oag/device/usb_device_classifier.h"
#include "oag/feedback/rumble_command.h"
#include "oag/feedback/keyboard_led_state.h"
#include "oag/firmware/bluetooth_runtime.h"
#include "oag/firmware/pc_xinput_platform_driver.h"
#include "oag/firmware/usb_pio_host.h"
#include "oag/firmware/xinput_host.h"
#include "oag/input/gamepad_state.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/pass_through_mapping.h"
#include "oag/mapping/pen_tablet_mapper.h"
#include "oag/mapping/touchscreen_mapper.h"
#include "oag/output/digitizer/digitizer_state.h"
#include "oag/output/universal_output_mode.h"
#include "oag/protocol/hid/boot_keyboard_input_driver.h"
#include "oag/protocol/hid/boot_mouse_input_driver.h"
#include "oag/protocol/hid/generic_hid_gamepad_driver.h"
#include "oag/protocol/xusb/xusb_input_driver.h"
#include "oag/protocol/xgip/xgip_input_driver.h"
#include "oag/transport/host_root_reconciler.h"

namespace {

enum class XgipInitPhase : std::uint8_t {
    None = 0,
    Power,
    SystemInit,
    ExtraInput,
    Led,
    AuthDone,
    Ready,
};

static constexpr std::uint8_t kXonePowerOn[] = {
    0x05, 0x20, 0x00, 0x01, 0x00
};

static constexpr std::uint8_t kXoneSystemInit[] = {
    0x05, 0x20, 0x00, 0x0F, 0x06
};

static constexpr std::uint8_t kXoneExtraInput[] = {
    0x4D, 0x10, 0x01, 0x02, 0x07, 0x00
};

static constexpr std::uint8_t kXoneLedOn[] = {
    0x0A, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14
};

static constexpr std::uint8_t kXoneAuthDone[] = {
    0x06, 0x20, 0x00, 0x02, 0x01, 0x00
};

class FirmwareCore final
    : public oag::firmware::BluetoothRuntimeObserver {
public:
    bool start() {
        // Golden baseline transport invariant: USB Host runs at 120 MHz to
        // avoid marginal PIO USB timing on the Pico 2 W / RP2350.
        if (!set_sys_clock_khz(120000, true)) {
            return false;
        }

        if (!tud_init(0)) {
            return false;
        }

        // PIO USB Host must remain initialized before future CYW43/BT start.
        if (!usbHost_.start()) {
            return false;
        }

        // Preserve the proven Golden/U6E ordering invariant: PIO USB Host is
        // live before CYW43/BTstack. Bluetooth failure is fail-soft so the
        // hardware-verified USB path remains usable.
        bluetoothAvailable_ = bluetooth_.initialize(*this);

        if (!platformOutput_.initialize()) {
            return false;
        }

        return true;
    }

    void task() {
        tud_task();
        platformOutput_.poll();

        usbHost_.task();

        if (bluetoothAvailable_) {
            bluetooth_.poll();
        }

        serviceKeyboardLeds();
        maintainXinputTransport();
        serviceXgipInit();
        serviceMouseAimRelease();
        servicePlatformFeedback();
    }

    void onUsbDeviceMounted(std::uint8_t devAddr) {
        rememberMountedRoot(devAddr);
    }

    void onXinputMounted(
        std::uint8_t devAddr,
        std::uint8_t instance,
        std::uint8_t type
    ) {
        std::uint16_t vid = 0;
        std::uint16_t pid = 0;

        if (!tuh_vid_pid_get(devAddr, &vid, &pid)) {
            return;
        }

        const oag::ProtocolKind protocol =
            type == OAG_XINPUT_XBOXONE
                ? oag::ProtocolKind::XgipXboxOne
                : oag::ProtocolKind::XusbXbox360;

        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.connectUsb(
            handle,
            vid,
            pid,
            protocol
        );

        if (!id) {
            return;
        }

        const auto slot = slots_.bindFirstFree(*id);
        if (!slot) {
            registry_.disconnect(*id);
            return;
        }

        states_[*slot] = {};
        states_[*slot].source = *id;
        states_[*slot].connected = true;

        if (*slot < pendingRumbleValid_.size()) {
            pendingRumble_[*slot] = {};
            pendingRumbleValid_[*slot] = false;
        }

        xgipPhases_[*slot] =
            protocol == oag::ProtocolKind::XgipXboxOne
                ? XgipInitPhase::Power
                : XgipInitPhase::None;
        xgipTxPending_[*slot] = false;
        xgipGuidePressed_[*slot] = false;
    }

    void onXinputUnmounted(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.findUsb(handle);
        if (!id) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr ||
            (
                record->protocol != oag::ProtocolKind::XusbXbox360 &&
                record->protocol != oag::ProtocolKind::XgipXboxOne
            )) {
            return;
        }

        const auto slot = slots_.slotFor(*id);

        if (slot) {
            states_[*slot] = {};
            xgipPhases_[*slot] = XgipInitPhase::None;
            xgipTxPending_[*slot] = false;
            xgipGuidePressed_[*slot] = false;

            if (*slot < pendingRumbleValid_.size()) {
                pendingRumble_[*slot] = {};
                pendingRumbleValid_[*slot] = false;
            }
        }

        slots_.release(*id);
        registry_.disconnect(*id);

        if (slot) {
            sendSlotOutput(*slot);
        }
    }

    void onHidMounted(
        std::uint8_t devAddr,
        std::uint8_t instance,
        const std::uint8_t* reportDescriptor,
        std::uint16_t reportDescriptorLength
    ) {
        const std::uint8_t protocol =
            tuh_hid_interface_protocol(devAddr, instance);

        std::uint16_t vid = 0;
        std::uint16_t pid = 0;
        if (!tuh_vid_pid_get(devAddr, &vid, &pid)) {
            return;
        }

        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        if (protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            const auto id = registry_.connectUsb(
                handle,
                vid,
                pid,
                oag::ProtocolKind::HidKeyboard
            );

            if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
                return;
            }

            keyboardStates_[id->index] = {};
            keyboardStates_[id->index].source = *id;
            keyboardStates_[id->index].connected = true;

            keyboardLedStates_[id->index].reset();
            keyboardLedDesired_[id->index] =
                keyboardLedStates_[id->index].reportByte();
            keyboardLedApplied_[id->index] = 0xFF;
            keyboardLedInFlight_[id->index] = 0;
            keyboardLedTxPending_[id->index] = false;
            return;
        }

        if (protocol == HID_ITF_PROTOCOL_MOUSE) {
            const auto id = registry_.connectUsb(
                handle,
                vid,
                pid,
                oag::ProtocolKind::HidMouse
            );

            if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
                return;
            }

            mouseStates_[id->index] = {};
            mouseStates_[id->index].source = *id;
            mouseStates_[id->index].connected = true;
            return;
        }

        if (
            reportDescriptor == nullptr ||
            reportDescriptorLength == 0
        ) {
            return;
        }

        oag::GenericHidGamepadQuirks quirks =
            genericHidQuirksFor(vid, pid);

        oag::GenericHidGamepadDescriptor descriptor {};

        bool parsed = genericHid_.parseDescriptor(
            reportDescriptor,
            reportDescriptorLength,
            quirks,
            descriptor
        );

        if (!parsed &&
            genericHid_.looksLikeGamepadDescriptor(
                reportDescriptor,
                reportDescriptorLength
            )) {
            // Some inexpensive controllers expose correct gamepad fields
            // under a vendor/composite top-level collection. Force only after
            // structural X/Y + buttons/hat evidence is present.
            quirks.forceGamepad = true;

            parsed = genericHid_.parseDescriptor(
                reportDescriptor,
                reportDescriptorLength,
                quirks,
                descriptor
            );
        }

        if (!parsed) {
            return;
        }

        const auto id = registry_.connectUsb(
            handle,
            vid,
            pid,
            oag::ProtocolKind::HidGamepad
        );

        if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
            return;
        }

        const auto slot = slots_.bindFirstFree(*id);
        if (!slot) {
            registry_.disconnect(*id);
            return;
        }

        genericHidDescriptors_[id->index] = descriptor;
        genericHidQuirks_[id->index] = quirks;

        states_[*slot] = {};
        states_[*slot].source = *id;
        states_[*slot].connected = true;

        if (*slot < pendingRumbleValid_.size()) {
            pendingRumble_[*slot] = {};
            pendingRumbleValid_[*slot] = false;
        }
    }

    void onHidUnmounted(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.findUsb(handle);
        if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr) {
            return;
        }

        if (record->protocol == oag::ProtocolKind::HidKeyboard) {
            keyboardStates_[id->index] = {};
            keyboardLedStates_[id->index].reset();
            keyboardLedDesired_[id->index] = 0;
            keyboardLedApplied_[id->index] = 0;
            keyboardLedInFlight_[id->index] = 0;
            keyboardLedTxPending_[id->index] = false;

            registry_.disconnect(*id);
            sendComposedOutput();
            return;
        }

        if (record->protocol == oag::ProtocolKind::HidMouse) {
            mouseStates_[id->index] = {};
            registry_.disconnect(*id);

            currentMouseMotion_ = {};
            mouseAimActive_ = false;
            sendComposedOutput();
            return;
        }

        if (record->protocol != oag::ProtocolKind::HidGamepad) {
            return;
        }

        const auto slot = slots_.slotFor(*id);

        if (slot && *slot < states_.size()) {
            states_[*slot] = {};

            if (*slot < pendingRumbleValid_.size()) {
                pendingRumble_[*slot] = {};
                pendingRumbleValid_[*slot] = false;
            }
        }

        slots_.release(*id);
        genericHidDescriptors_[id->index] = {};
        genericHidQuirks_[id->index] = {};
        registry_.disconnect(*id);

        if (slot) {
            sendSlotOutput(*slot);
        }
    }

    void onHidReport(
        std::uint8_t devAddr,
        std::uint8_t instance,
        const std::uint8_t* report,
        std::uint16_t length
    ) {
        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.findUsb(handle);
        if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr) {
            return;
        }

        if (record->protocol == oag::ProtocolKind::HidKeyboard) {
            const oag::KeyboardState previous =
                keyboardStates_[id->index];

            if (keyboard_.parse(
                    *id,
                    report,
                    length,
                    time_us_64(),
                    keyboardStates_[id->index]
                )) {
                if (keyboardLedStates_[id->index].updateFromKeyEdges(
                        previous,
                        keyboardStates_[id->index]
                    )) {
                    keyboardLedDesired_[id->index] =
                        keyboardLedStates_[id->index].reportByte();
                }

                sendComposedOutput();
            }
            return;
        }

        if (record->protocol == oag::ProtocolKind::HidMouse) {
            if (mouse_.parse(
                    *id,
                    report,
                    length,
                    time_us_64(),
                    mouseStates_[id->index]
                )) {
                const oag::MouseState& mouseState =
                    mouseStates_[id->index];

                currentMouseMotion_ = {
                    mouseState.dx,
                    mouseState.dy,
                };

                mouseAimActive_ =
                    currentMouseMotion_.dx != 0 ||
                    currentMouseMotion_.dy != 0;

                if (mouseAimActive_) {
                    mouseAimExpiresUs_ =
                        time_us_64() + kMouseAimHoldUs;
                }

                const oag::UniversalOutputRouting routing =
                    outputMode_.routing();

                if (routing.touchscreen) {
                    touchState_ = touchscreenMapper_.apply(
                        mouseState,
                        currentMouseMotion_
                    );
                }

                if (routing.penTablet) {
                    penState_ = penTabletMapper_.apply(
                        mouseState,
                        currentMouseMotion_
                    );
                }

                sendComposedOutput();
            }
            return;
        }

        if (record->protocol != oag::ProtocolKind::HidGamepad) {
            return;
        }

        const auto slot = slots_.slotFor(*id);
        if (!slot || *slot >= states_.size()) {
            return;
        }

        if (!genericHid_.parseReport(
                *id,
                genericHidDescriptors_[id->index],
                genericHidQuirks_[id->index],
                report,
                length,
                time_us_64(),
                states_[*slot]
            )) {
            return;
        }

        sendSlotOutput(*slot);
    }

    void onUsbDeviceUnmounted(std::uint8_t devAddr) {
        forgetMountedRoot(devAddr);

        for (std::uint8_t instance = 0;
             instance < CFG_TUH_XINPUT;
             ++instance) {
            onXinputUnmounted(devAddr, instance);
        }
    }

    void onXinputReport(
        std::uint8_t devAddr,
        std::uint8_t instance,
        const std::uint8_t* report,
        std::uint16_t length
    ) {
        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.findUsb(handle);
        if (!id) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr) {
            return;
        }

        const auto slot = slots_.slotFor(*id);
        if (!slot || *slot >= states_.size()) {
            return;
        }

        bool parsed = false;

        if (record->protocol == oag::ProtocolKind::XusbXbox360) {
            parsed = xusb_.parse(
                *id,
                report,
                length,
                time_us_64(),
                states_[*slot]
            );
        } else if (record->protocol == oag::ProtocolKind::XgipXboxOne) {
            if (report != nullptr && length != 0 && report[0] == 0x02) {
                xgipPhases_[*slot] = XgipInitPhase::Power;
                xgipTxPending_[*slot] = false;
                return;
            }

            if (report != nullptr && length >= 5 && report[0] == 0x07) {
                xgipGuidePressed_[*slot] = report[4] == 0x01;

                if (xgipGuidePressed_[*slot]) {
                    states_[*slot].buttons |= oag::ButtonGuide;
                } else {
                    states_[*slot].buttons &= ~oag::ButtonGuide;
                }

                parsed = true;
            } else {
                parsed = xgip_.parse(
                    *id,
                    report,
                    length,
                    time_us_64(),
                    states_[*slot]
                );

                if (parsed && xgipGuidePressed_[*slot]) {
                    states_[*slot].buttons |= oag::ButtonGuide;
                }
            }
        }

        if (!parsed) {
            return;
        }

        sendSlotOutput(*slot);
    }

    void onXinputReportSent(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        handleXinputReportSent(devAddr, instance);
    }

    void onHidSetReportComplete(
        std::uint8_t devAddr,
        std::uint8_t instance,
        std::uint16_t length
    ) {
        const auto id = registry_.findUsb(
            oag::UsbTransportHandle {devAddr, instance}
        );

        if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr ||
            record->protocol != oag::ProtocolKind::HidKeyboard) {
            return;
        }

        keyboardLedTxPending_[id->index] = false;

        if (length != 0) {
            keyboardLedApplied_[id->index] =
                keyboardLedInFlight_[id->index];
        }
    }

    void onBluetoothHidDescriptor(
        oag::TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength
    ) override {
        if (
            descriptor == nullptr ||
            descriptorLength == 0
        ) {
            return;
        }

        const oag::BluetoothTransportHandle handle {
            transport,
            connectionHandle,
            serviceInstance,
        };

        if (registry_.findBluetooth(handle)) {
            return;
        }

        oag::GenericHidGamepadQuirks quirks {};
        oag::GenericHidGamepadDescriptor parsed {};

        bool isGamepad = genericHid_.parseDescriptor(
            descriptor,
            descriptorLength,
            quirks,
            parsed
        );

        if (
            !isGamepad &&
            genericHid_.looksLikeGamepadDescriptor(
                descriptor,
                descriptorLength
            )
        ) {
            quirks.forceGamepad = true;
            isGamepad = genericHid_.parseDescriptor(
                descriptor,
                descriptorLength,
                quirks,
                parsed
            );
        }

        // U8A live path intentionally binds only descriptors that prove
        // gamepad structure. BT keyboard/mouse canonical parsing is a
        // separate driver gate so they cannot be misclassified as pads.
        if (!isGamepad) {
            return;
        }

        const auto id = registry_.connectBluetooth(
            handle,
            0,
            0,
            oag::ProtocolKind::HidGamepad
        );

        if (
            !id ||
            id->index >= oag::DeviceRegistry::kCapacity
        ) {
            return;
        }

        const auto slot = slots_.bindFirstFree(*id);
        if (!slot) {
            registry_.disconnect(*id);
            return;
        }

        genericHidDescriptors_[id->index] = parsed;
        genericHidQuirks_[id->index] = quirks;

        states_[*slot] = {};
        states_[*slot].source = *id;
        states_[*slot].connected = true;

        if (*slot < pendingRumbleValid_.size()) {
            pendingRumble_[*slot] = {};
            pendingRumbleValid_[*slot] = false;
        }

        sendSlotOutput(*slot);
    }

    void onBluetoothHidReport(
        oag::TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        const std::uint8_t* report,
        std::size_t reportLength
    ) override {
        const oag::BluetoothTransportHandle handle {
            transport,
            connectionHandle,
            serviceInstance,
        };

        const auto id = registry_.findBluetooth(handle);
        if (
            !id ||
            id->index >= oag::DeviceRegistry::kCapacity
        ) {
            return;
        }

        const auto slot = slots_.slotFor(*id);
        if (!slot || *slot >= states_.size()) {
            return;
        }

        if (!genericHid_.parseReport(
                *id,
                genericHidDescriptors_[id->index],
                genericHidQuirks_[id->index],
                report,
                reportLength,
                time_us_64(),
                states_[*slot]
            )) {
            return;
        }

        sendSlotOutput(*slot);
    }

    void onBluetoothHidDisconnected(
        oag::TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance
    ) override {
        const oag::BluetoothTransportHandle handle {
            transport,
            connectionHandle,
            serviceInstance,
        };

        const auto id = registry_.findBluetooth(handle);
        if (
            !id ||
            id->index >= oag::DeviceRegistry::kCapacity
        ) {
            return;
        }

        const auto slot = slots_.slotFor(*id);

        if (slot && *slot < states_.size()) {
            states_[*slot] = {};

            if (*slot < pendingRumbleValid_.size()) {
                pendingRumble_[*slot] = {};
                pendingRumbleValid_[*slot] = false;
            }
        }

        slots_.release(*id);
        genericHidDescriptors_[id->index] = {};
        genericHidQuirks_[id->index] = {};
        registry_.disconnect(*id);

        if (slot) {
            sendSlotOutput(*slot);
        }
    }

private:
    static constexpr std::uint8_t kRootCount = 3;
    static constexpr std::uint64_t kMouseAimHoldUs = 6000;

    static oag::GenericHidGamepadQuirks genericHidQuirksFor(
        std::uint16_t vid,
        std::uint16_t pid
    ) {
        const oag::UsbDeviceClassification classification =
            oag::UsbDeviceClassifier {}.classify(
                oag::UsbDeviceProbe {
                    vid,
                    pid,
                    0x03,
                    0x00,
                    0x00,
                    0,
                }
            );

        oag::GenericHidGamepadQuirks quirks {};
        quirks.forceGamepad =
            classification.hasQuirk(oag::UsbQuirkForceHidGamepad);
        quirks.zRzAsRightStick =
            classification.hasQuirk(oag::UsbQuirkZRzAsRightStick);

        return quirks;
    }

    void rememberMountedRoot(std::uint8_t devAddr) {
        if (devAddr >= rootByDevice_.size()) {
            return;
        }

        const std::uint8_t rhport = usbh_get_rhport(devAddr);
        if (rhport < 1 || rhport > kRootCount) {
            return;
        }

        rootByDevice_[devAddr] = rhport;
        rebuildMountedRootMask();
    }

    void forgetMountedRoot(std::uint8_t devAddr) {
        if (devAddr >= rootByDevice_.size()) {
            return;
        }

        rootByDevice_[devAddr] = 0;
        rebuildMountedRootMask();
    }

    void rebuildMountedRootMask() {
        std::uint8_t mask = 0;

        for (const std::uint8_t rhport : rootByDevice_) {
            if (rhport >= 1 && rhport <= kRootCount) {
                mask |= static_cast<std::uint8_t>(
                    1u << static_cast<std::uint8_t>(rhport - 1u)
                );
            }
        }

        mountedRootMask_ = mask;
    }

    void serviceKeyboardLeds() {
        for (std::size_t i = 0;
             i < keyboardStates_.size();
             ++i) {
            if (!keyboardStates_[i].connected ||
                keyboardLedTxPending_[i] ||
                keyboardLedDesired_[i] == keyboardLedApplied_[i]) {
                continue;
            }

            const oag::DeviceRecord* record =
                registry_.find(keyboardStates_[i].source);

            if (record == nullptr ||
                record->protocol != oag::ProtocolKind::HidKeyboard) {
                continue;
            }

            keyboardLedInFlight_[i] = keyboardLedDesired_[i];

            if (tuh_hid_set_report(
                    record->usb.deviceAddress,
                    record->usb.interfaceInstance,
                    0,
                    HID_REPORT_TYPE_OUTPUT,
                    &keyboardLedInFlight_[i],
                    1
                )) {
                keyboardLedTxPending_[i] = true;
            }
        }
    }

    void maintainXinputTransport() {
        // Port the proven Golden maintenance behavior: continuously make
        // sure each mounted gameplay IN endpoint stays armed. This recovers
        // idle/busy transitions without synthesizing root REMOVE/ATTACH
        // events or mutating PIO endpoint internals.
        for (std::size_t i = 0;
             i < oag::LogicalSlotManager::kGamepadSlots;
             ++i) {
            const auto slot = static_cast<oag::LogicalSlotId>(i);
            const oag::DeviceId source = slots_.deviceFor(slot);

            if (!source.valid()) {
                continue;
            }

            const oag::DeviceRecord* record = registry_.find(source);
            if (record == nullptr ||
                (
                    record->protocol != oag::ProtocolKind::XusbXbox360 &&
                    record->protocol != oag::ProtocolKind::XgipXboxOne
                )) {
                continue;
            }

            const std::uint8_t devAddr = record->usb.deviceAddress;
            const std::uint8_t instance = record->usb.interfaceInstance;

            if (tuh_xinput_mounted(devAddr, instance) &&
                tuh_xinput_ready(devAddr, instance)) {
                tuh_xinput_receive_report(devAddr, instance);
            }
        }
    }

    void serviceXgipInit() {
        for (std::size_t i = 0;
             i < oag::LogicalSlotManager::kGamepadSlots;
             ++i) {
            const auto slot = static_cast<oag::LogicalSlotId>(i);
            const oag::DeviceId source = slots_.deviceFor(slot);
            const oag::DeviceRecord* record = registry_.find(source);

            if (record == nullptr ||
                record->protocol != oag::ProtocolKind::XgipXboxOne ||
                xgipPhases_[i] == XgipInitPhase::None ||
                xgipPhases_[i] == XgipInitPhase::Ready ||
                xgipTxPending_[i]) {
                continue;
            }

            const std::uint8_t* packet = nullptr;
            std::uint16_t packetLength = 0;

            switch (xgipPhases_[i]) {
                case XgipInitPhase::Power:
                    packet = kXonePowerOn;
                    packetLength = sizeof(kXonePowerOn);
                    break;
                case XgipInitPhase::SystemInit:
                    packet = kXoneSystemInit;
                    packetLength = sizeof(kXoneSystemInit);
                    break;
                case XgipInitPhase::ExtraInput:
                    packet = kXoneExtraInput;
                    packetLength = sizeof(kXoneExtraInput);
                    break;
                case XgipInitPhase::Led:
                    packet = kXoneLedOn;
                    packetLength = sizeof(kXoneLedOn);
                    break;
                case XgipInitPhase::AuthDone:
                    packet = kXoneAuthDone;
                    packetLength = sizeof(kXoneAuthDone);
                    break;
                default:
                    break;
            }

            if (packet != nullptr &&
                tuh_xinput_send_report(
                    record->usb.deviceAddress,
                    record->usb.interfaceInstance,
                    packet,
                    packetLength
                )) {
                xgipTxPending_[i] = true;
            }
        }
    }

    void advanceXgipInit(oag::LogicalSlotId slot) {
        if (slot >= xgipPhases_.size()) {
            return;
        }

        const oag::DeviceId source = slots_.deviceFor(slot);
        const oag::DeviceRecord* record = registry_.find(source);

        if (record == nullptr ||
            record->protocol != oag::ProtocolKind::XgipXboxOne) {
            return;
        }

        switch (xgipPhases_[slot]) {
            case XgipInitPhase::Power:
                if (record->vid == 0x045E &&
                    (record->pid == 0x02EA || record->pid == 0x0B00)) {
                    xgipPhases_[slot] = XgipInitPhase::SystemInit;
                } else {
                    xgipPhases_[slot] = XgipInitPhase::Led;
                }
                break;
            case XgipInitPhase::SystemInit:
                xgipPhases_[slot] =
                    record->vid == 0x045E && record->pid == 0x0B00
                        ? XgipInitPhase::ExtraInput
                        : XgipInitPhase::Led;
                break;
            case XgipInitPhase::ExtraInput:
                xgipPhases_[slot] = XgipInitPhase::Led;
                break;
            case XgipInitPhase::Led:
                xgipPhases_[slot] = XgipInitPhase::AuthDone;
                break;
            case XgipInitPhase::AuthDone:
                xgipPhases_[slot] = XgipInitPhase::Ready;
                break;
            default:
                break;
        }
    }

    void handleXinputReportSent(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        const auto id = registry_.findUsb(
            oag::UsbTransportHandle {devAddr, instance}
        );

        if (!id) {
            return;
        }

        const oag::DeviceRecord* record = registry_.find(*id);
        if (record == nullptr ||
            record->protocol != oag::ProtocolKind::XgipXboxOne) {
            return;
        }

        const auto slot = slots_.slotFor(*id);
        if (!slot || *slot >= xgipTxPending_.size()) {
            return;
        }

        if (xgipTxPending_[*slot]) {
            xgipTxPending_[*slot] = false;
            advanceXgipInit(*slot);
        }
    }

    oag::KeyboardState combinedKeyboard() const {
        oag::KeyboardState combined {};

        for (const oag::KeyboardState& state : keyboardStates_) {
            if (!state.connected) {
                continue;
            }

            combined.connected = true;
            combined.modifiers |= state.modifiers;

            for (std::size_t i = 0;
                 i < oag::KeyboardState::kWordCount;
                 ++i) {
                combined.usages[i] |= state.usages[i];
            }

            if (state.timestampUs > combined.timestampUs) {
                combined.timestampUs = state.timestampUs;
            }
        }

        return combined;
    }

    oag::MouseState combinedMouse() const {
        oag::MouseState combined {};

        for (const oag::MouseState& state : mouseStates_) {
            if (!state.connected) {
                continue;
            }

            combined.connected = true;
            combined.buttons |= state.buttons;

            if (state.timestampUs > combined.timestampUs) {
                combined.timestampUs = state.timestampUs;
            }
        }

        return combined;
    }

    oag::LogicalGamepadState baseSlotZeroOutput() const {
        if (states_.empty() || !states_[0].connected) {
            return {};
        }

        return mapping_.process(states_[0]);
    }

    void sendSlotOutput(oag::LogicalSlotId slot) {
        if (slot >= oag::firmware::PcXinputDevice::kOutputSlots) {
            return;
        }

        if (slot == 0) {
            sendComposedOutput();
            return;
        }

        oag::LogicalGamepadState output {};

        if (slot < states_.size() && states_[slot].connected) {
            output = mapping_.process(states_[slot]);
        } else {
            // The four PC interfaces are statically enumerated. Vacant OAG
            // slots therefore remain present on Windows but report neutral.
            output.connected = true;
        }

        platformOutput_.submit(slot, output);
    }

    void sendComposedOutput() {
        const oag::KeyboardState keyboard = combinedKeyboard();
        const oag::MouseState mouse = combinedMouse();

        const bool hasKeyboard = keyboard.connected;
        const bool hasMouse = mouse.connected;

        oag::LogicalGamepadState output =
            keyboardMouse_.apply(
                hasKeyboard ? &keyboard : nullptr,
                hasMouse ? &mouse : nullptr,
                mouseAimActive_
                    ? currentMouseMotion_
                    : oag::MouseMotion {},
                baseSlotZeroOutput()
            );

        if (!output.connected && !hasKeyboard && !hasMouse) {
            oag::LogicalGamepadState neutral {};
            neutral.connected = true;
            platformOutput_.submit(0, neutral);
            return;
        }

        platformOutput_.submit(0, output);
    }

    void serviceMouseAimRelease() {
        if (!mouseAimActive_) {
            return;
        }

        if (time_us_64() < mouseAimExpiresUs_) {
            return;
        }

        currentMouseMotion_ = {};
        mouseAimActive_ = false;
        sendComposedOutput();
    }

    void servicePlatformFeedback() {
        std::uint8_t feedbackSlot = 0;
        oag::RumbleCommand newest {};

        // Drain all currently completed PC OUT reports. There are only four
        // PC XInput outputs, so this loop is bounded by device-side state.
        while (platformOutput_.takeRumble(feedbackSlot, newest)) {
            if (feedbackSlot >= pendingRumble_.size()) {
                continue;
            }

            pendingRumble_[feedbackSlot] = newest;
            pendingRumbleValid_[feedbackSlot] = true;
        }

        for (std::size_t i = 0;
             i < pendingRumbleValid_.size();
             ++i) {
            if (!pendingRumbleValid_[i]) {
                continue;
            }

            const auto slot = static_cast<oag::LogicalSlotId>(i);
            const oag::DeviceId source = slots_.deviceFor(slot);
            const oag::DeviceRecord* record = registry_.find(source);

            if (record == nullptr) {
                pendingRumbleValid_[i] = false;
                continue;
            }

            if (record->protocol == oag::ProtocolKind::XusbXbox360) {
                const std::uint8_t rumblePacket[8] = {
                    0x00,
                    0x08,
                    0x00,
                    pendingRumble_[i].leftMotor,
                    pendingRumble_[i].rightMotor,
                    0x00,
                    0x00,
                    0x00,
                };

                if (tuh_xinput_send_report(
                        record->usb.deviceAddress,
                        record->usb.interfaceInstance,
                        rumblePacket,
                        sizeof(rumblePacket)
                    )) {
                    pendingRumbleValid_[i] = false;
                }

                continue;
            }

            if (record->protocol == oag::ProtocolKind::XgipXboxOne) {
                if (i >= xgipPhases_.size() ||
                    xgipPhases_[i] != XgipInitPhase::Ready ||
                    xgipTxPending_[i]) {
                    continue;
                }

                const std::uint8_t rumblePacket[13] = {
                    0x09, 0x00, xgipRumbleSequence_[i], 0x09,
                    0x00, 0x0F,
                    0x00, 0x00,
                    pendingRumble_[i].leftMotor,
                    pendingRumble_[i].rightMotor,
                    0xFF, 0x00, 0xFF,
                };

                if (tuh_xinput_send_report(
                        record->usb.deviceAddress,
                        record->usb.interfaceInstance,
                        rumblePacket,
                        sizeof(rumblePacket)
                    )) {
                    pendingRumbleValid_[i] = false;
                    ++xgipRumbleSequence_[i];
                    if (xgipRumbleSequence_[i] == 0) {
                        xgipRumbleSequence_[i] = 1;
                    }
                }

                continue;
            }

            // Generic HID output feedback is family-specific and remains a
            // later driver capability. Do not emit guessed vendor reports.
            pendingRumbleValid_[i] = false;
        }
    }

    oag::firmware::UsbPioHost usbHost_;
    oag::firmware::BluetoothRuntime bluetooth_;
    bool bluetoothAvailable_ = false;

    oag::DeviceRegistry registry_;
    oag::LogicalSlotManager slots_;
    oag::XusbInputDriver xusb_;
    oag::XgipInputDriver xgip_;
    oag::BootKeyboardInputDriver keyboard_;
    oag::BootMouseInputDriver mouse_;
    oag::GenericHidGamepadDriver genericHid_;
    oag::PassThroughMapping mapping_;
    oag::KeyboardMouseGamepadMapper keyboardMouse_;

    oag::UniversalOutputModeRouter outputMode_;
    oag::TouchscreenMapper touchscreenMapper_;
    oag::PenTabletMapper penTabletMapper_;
    oag::TouchDigitizerState touchState_ {};
    oag::PenDigitizerState penState_ {};

    oag::firmware::PcXinputPlatformDriver platformOutput_;

    std::array<
        oag::UniversalGamepadState,
        oag::LogicalSlotManager::kGamepadSlots
    > states_ {};

    std::array<
        oag::KeyboardState,
        oag::DeviceRegistry::kCapacity
    > keyboardStates_ {};

    std::array<
        oag::MouseState,
        oag::DeviceRegistry::kCapacity
    > mouseStates_ {};

    std::array<
        oag::KeyboardLedState,
        oag::DeviceRegistry::kCapacity
    > keyboardLedStates_ {};

    std::array<
        std::uint8_t,
        oag::DeviceRegistry::kCapacity
    > keyboardLedDesired_ {};

    std::array<
        std::uint8_t,
        oag::DeviceRegistry::kCapacity
    > keyboardLedApplied_ {};

    std::array<
        std::uint8_t,
        oag::DeviceRegistry::kCapacity
    > keyboardLedInFlight_ {};

    std::array<
        bool,
        oag::DeviceRegistry::kCapacity
    > keyboardLedTxPending_ {};

    std::array<
        oag::GenericHidGamepadDescriptor,
        oag::DeviceRegistry::kCapacity
    > genericHidDescriptors_ {};

    std::array<
        oag::GenericHidGamepadQuirks,
        oag::DeviceRegistry::kCapacity
    > genericHidQuirks_ {};

    std::array<
        std::uint8_t,
        CFG_TUH_DEVICE_MAX + 1
    > rootByDevice_ {};

    std::uint8_t mountedRootMask_ = 0;

    oag::MouseMotion currentMouseMotion_ {};
    std::uint64_t mouseAimExpiresUs_ = 0;
    bool mouseAimActive_ = false;

    std::array<
        XgipInitPhase,
        oag::LogicalSlotManager::kGamepadSlots
    > xgipPhases_ {};

    std::array<
        bool,
        oag::LogicalSlotManager::kGamepadSlots
    > xgipTxPending_ {};

    std::array<
        bool,
        oag::LogicalSlotManager::kGamepadSlots
    > xgipGuidePressed_ {};

    std::array<
        std::uint8_t,
        oag::firmware::PcXinputDevice::kOutputSlots
    > xgipRumbleSequence_ {1, 1, 1, 1};

    std::array<
        oag::RumbleCommand,
        oag::firmware::PcXinputDevice::kOutputSlots
    > pendingRumble_ {};

    std::array<
        bool,
        oag::firmware::PcXinputDevice::kOutputSlots
    > pendingRumbleValid_ {};
};

FirmwareCore gCore;

} // namespace

extern "C" void tuh_mount_cb(std::uint8_t dev_addr) {
    gCore.onUsbDeviceMounted(dev_addr);
}

extern "C" void tuh_umount_cb(std::uint8_t dev_addr) {
    gCore.onUsbDeviceUnmounted(dev_addr);
}

extern "C" void tuh_hid_mount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* desc_report,
    std::uint16_t desc_len
) {
    gCore.onHidMounted(
        dev_addr,
        instance,
        desc_report,
        desc_len
    );
    tuh_hid_receive_report(dev_addr, instance);
}

extern "C" void tuh_hid_umount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    gCore.onHidUnmounted(dev_addr, instance);
}

extern "C" void tuh_hid_report_received_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    gCore.onHidReport(dev_addr, instance, report, len);
    tuh_hid_receive_report(dev_addr, instance);
}

extern "C" void tuh_hid_set_report_complete_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t report_id,
    std::uint8_t report_type,
    std::uint16_t len
) {
    (void)report_id;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        gCore.onHidSetReportComplete(
            dev_addr,
            instance,
            len
        );
    }
}

extern "C" void tuh_xinput_mount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t type,
    std::uint8_t subtype
) {
    (void)type;
    (void)subtype;
    gCore.onXinputMounted(dev_addr, instance, type);
}

extern "C" void tuh_xinput_umount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    gCore.onXinputUnmounted(dev_addr, instance);
}

extern "C" void tuh_xinput_report_received_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    gCore.onXinputReport(dev_addr, instance, report, len);
}

extern "C" void tuh_xinput_report_sent_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    (void)report;
    (void)len;
    gCore.onXinputReportSent(dev_addr, instance);
}

int main() {
    if (!gCore.start()) {
        while (true) {
            tight_loop_contents();
        }
    }

    while (true) {
        gCore.task();
        tight_loop_contents();
    }
}
