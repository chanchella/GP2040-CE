#include <array>
#include <cstdint>
#include <cstring>
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
#include "oag/firmware/bluetooth_hid_parser_v2.h"
#include "oag/firmware/bluetooth_host_v2.h"
#include "oag/firmware/joypados_ble_output.h"
#include "oag/firmware/pc_xinput_platform_driver.h"
#include "oag/firmware/pc_native_km_output.h"
#include "oag/firmware/usb_pio_host.h"
#include "oag/firmware/xinput_host.h"
#include "oag/input/gamepad_state.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/native_km_combo_engine.h"
#include "oag/mapping/pass_through_mapping.h"
#include "oag/protocol/hid/boot_keyboard_input_driver.h"
#include "oag/protocol/hid/boot_mouse_input_driver.h"
#include "oag/protocol/hid/generic_hid_gamepad_driver.h"
#include "oag/protocol/xusb/xusb_input_driver.h"
#include "oag/protocol/xgip/xgip_input_driver.h"
#include "oag/transport/host_root_reconciler.h"

namespace {

enum class KeyboardMouseOutputMode : std::uint8_t {
    Native = 0,
    Controller,
};

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
    : public oag::firmware::BluetoothHostV2Observer {
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

        // Hardware-verified invariant: PIO USB Host owns the board first.
        if (!usbHost_.start()) {
            return false;
        }

        // Golden G2E3 invariant: give PIO USB 100 ms to settle before
        // CYW43/BTstack is initialized. Bluetooth is fail-soft.
        bluetoothInitNotBeforeUs_ =
            time_us_64() + 100000ull;

        if (!platformOutput_.initialize()) {
            return false;
        }

        return true;
    }

    void task() {
        tud_task();
        platformOutput_.poll();
        servicePlatformPlayerAssignments();

        usbHost_.task();
        serviceBluetoothHostV2();

        serviceKeyboardLeds();
        maintainXinputTransport();
        serviceXgipInit();
        servicePrimaryControllerChords();
        serviceKeyboardMouseModeToggle();
        serviceNativeKeyboardMouseOutput();
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

        rebuildPcOutputRouting();
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

        bluetoothHost_.notifyWiredGamepadDetached(
            record->vid,
            record->pid
        );

        slots_.release(*id);
        registry_.disconnect(*id);

        rebuildPcOutputRouting();
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

            usbHidDescriptors_[id->index] = {};
            usbHidRawDescriptorLengths_[id->index] = 0;

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

            usbHidDescriptors_[id->index] = {};
            usbHidRawDescriptorLengths_[id->index] = 0;

            mouseStates_[id->index] = {};
            mouseStates_[id->index].source = *id;
            mouseStates_[id->index].connected = true;
            return;
        }

        if (
            reportDescriptor == nullptr ||
            reportDescriptorLength == 0 ||
            reportDescriptorLength > kMaxUsbHidDescriptorBytes
        ) {
            return;
        }

        oag::firmware::BluetoothHidDescriptorV2 hidInfo {};
        const bool hidInfoValid =
            bluetoothHidParser_.parseDescriptor(
                reportDescriptor,
                reportDescriptorLength,
                hidInfo
            );

        oag::GenericHidGamepadQuirks quirks =
            genericHidQuirksFor(vid, pid);

        oag::GenericHidGamepadDescriptor gamepadDescriptor {};
        bool parsedGamepad = false;

        if (
            (hidInfoValid && hidInfo.hasGamepad) ||
            genericHid_.looksLikeGamepadDescriptor(
                reportDescriptor,
                reportDescriptorLength
            )
        ) {
            parsedGamepad = genericHid_.parseDescriptor(
                reportDescriptor,
                reportDescriptorLength,
                quirks,
                gamepadDescriptor
            );

            if (
                !parsedGamepad &&
                genericHid_.looksLikeGamepadDescriptor(
                    reportDescriptor,
                    reportDescriptorLength
                )
            ) {
                quirks.forceGamepad = true;
                parsedGamepad = genericHid_.parseDescriptor(
                    reportDescriptor,
                    reportDescriptorLength,
                    quirks,
                    gamepadDescriptor
                );
            }
        }

        const bool hasKeyboard =
            hidInfoValid && hidInfo.hasKeyboard;
        const bool hasMouse =
            hidInfoValid && hidInfo.hasMouse;

        if (!parsedGamepad && !hasKeyboard && !hasMouse) {
            return;
        }

        oag::ProtocolKind primaryProtocol =
            oag::ProtocolKind::Unknown;

        if (parsedGamepad) {
            primaryProtocol = oag::ProtocolKind::HidGamepad;
        } else if (hasKeyboard) {
            primaryProtocol = oag::ProtocolKind::HidKeyboard;
        } else {
            primaryProtocol = oag::ProtocolKind::HidMouse;
        }

        const auto id = registry_.connectUsb(
            handle,
            vid,
            pid,
            primaryProtocol
        );

        if (!id || id->index >= oag::DeviceRegistry::kCapacity) {
            return;
        }

        usbHidDescriptors_[id->index] =
            hidInfoValid
                ? hidInfo
                : oag::firmware::BluetoothHidDescriptorV2 {};

        std::memcpy(
            usbHidRawDescriptors_[id->index].data(),
            reportDescriptor,
            reportDescriptorLength
        );
        usbHidRawDescriptorLengths_[id->index] =
            reportDescriptorLength;

        if (parsedGamepad) {
            const auto slot = slots_.bindFirstFree(*id);

            if (!slot) {
                if (!hasKeyboard && !hasMouse) {
                    usbHidDescriptors_[id->index] = {};
                    usbHidRawDescriptorLengths_[id->index] = 0;
                    registry_.disconnect(*id);
                    return;
                }
            } else {
                genericHidDescriptors_[id->index] =
                    gamepadDescriptor;
                genericHidQuirks_[id->index] = quirks;

                states_[*slot] = {};
                states_[*slot].source = *id;
                states_[*slot].connected = true;

                if (*slot < pendingRumbleValid_.size()) {
                    pendingRumble_[*slot] = {};
                    pendingRumbleValid_[*slot] = false;
                }

                rebuildPcOutputRouting();
            }
        }

        if (hasKeyboard) {
            keyboardStates_[id->index] = {};
            keyboardStates_[id->index].source = *id;
            keyboardStates_[id->index].connected = true;

            keyboardLedStates_[id->index].reset();
            keyboardLedDesired_[id->index] = 0;
            keyboardLedApplied_[id->index] = 0;
            keyboardLedInFlight_[id->index] = 0;
            keyboardLedTxPending_[id->index] = false;
        }

        if (hasMouse) {
            mouseStates_[id->index] = {};
            mouseStates_[id->index].source = *id;
            mouseStates_[id->index].connected = true;
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

        bool composedChanged = false;
        bool routingChanged = false;

        if (keyboardStates_[id->index].connected) {
            keyboardStates_[id->index] = {};
            keyboardLedStates_[id->index].reset();
            keyboardLedDesired_[id->index] = 0;
            keyboardLedApplied_[id->index] = 0;
            keyboardLedInFlight_[id->index] = 0;
            keyboardLedTxPending_[id->index] = false;
            composedChanged = true;
        }

        if (mouseStates_[id->index].connected) {
            mouseStates_[id->index] = {};
            currentMouseMotion_ = {};
            mouseAimActive_ = false;
            composedChanged = true;
        }

        const auto slot = slots_.slotFor(*id);

        if (slot && *slot < states_.size()) {
            states_[*slot] = {};

            if (*slot < pendingRumbleValid_.size()) {
                pendingRumble_[*slot] = {};
                pendingRumbleValid_[*slot] = false;
            }

            bluetoothHost_.notifyWiredGamepadDetached(
                record->vid,
                record->pid
            );

            slots_.release(*id);
            routingChanged = true;
        }

        genericHidDescriptors_[id->index] = {};
        genericHidQuirks_[id->index] = {};
        usbHidDescriptors_[id->index] = {};
        usbHidRawDescriptorLengths_[id->index] = 0;
        registry_.disconnect(*id);

        if (routingChanged) {
            rebuildPcOutputRouting();
        } else if (composedChanged) {
            sendComposedOutput();
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

        const auto& hidInfo =
            usbHidDescriptors_[id->index];

        if (hidInfo.valid) {
            const std::uint16_t descriptorLength =
                usbHidRawDescriptorLengths_[id->index];

            if (
                descriptorLength == 0 ||
                descriptorLength > kMaxUsbHidDescriptorBytes
            ) {
                return;
            }

            const std::uint8_t* descriptor =
                usbHidRawDescriptors_[id->index].data();

            const std::uint64_t nowUs = time_us_64();
            bool composedChanged = false;

            if (hidInfo.hasGamepad) {
                const auto slot = slots_.slotFor(*id);

                if (
                    slot &&
                    *slot < states_.size() &&
                    genericHidDescriptors_[id->index].valid &&
                    genericHid_.parseReport(
                        *id,
                        genericHidDescriptors_[id->index],
                        genericHidQuirks_[id->index],
                        report,
                        length,
                        nowUs,
                        states_[*slot]
                    )
                ) {
                    sendSlotOutput(*slot);
                }
            }

            if (
                hidInfo.hasKeyboard &&
                bluetoothHidParser_.parseKeyboard(
                    *id,
                    hidInfo,
                    descriptor,
                    descriptorLength,
                    report,
                    length,
                    nowUs,
                    keyboardStates_[id->index]
                )
            ) {
                composedChanged = true;
            }

            if (
                hidInfo.hasMouse &&
                bluetoothHidParser_.parseMouse(
                    *id,
                    hidInfo,
                    descriptor,
                    descriptorLength,
                    report,
                    length,
                    nowUs,
                    mouseStates_[id->index]
                )
            ) {
                const oag::MouseState& mouseState =
                    mouseStates_[id->index];

                currentMouseMotion_ = {
                    mouseState.dx,
                    mouseState.dy,
                };
                currentNativeWheel_ = mouseState.wheel;
                currentNativePan_ = mouseState.pan;

                mouseAimActive_ =
                    currentMouseMotion_.dx != 0 ||
                    currentMouseMotion_.dy != 0;

                if (mouseAimActive_) {
                    mouseAimExpiresUs_ =
                        nowUs + kMouseAimHoldUs;
                }

                composedChanged = true;
            }

            if (composedChanged) {
                sendComposedOutput();
            }

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
                currentNativeWheel_ = mouseState.wheel;
                currentNativePan_ = mouseState.pan;

                mouseAimActive_ =
                    currentMouseMotion_.dx != 0 ||
                    currentMouseMotion_.dy != 0;

                if (mouseAimActive_) {
                    mouseAimExpiresUs_ =
                        time_us_64() + kMouseAimHoldUs;
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
        if (
            record == nullptr ||
            record->protocol != oag::ProtocolKind::HidKeyboard ||
            usbHidDescriptors_[id->index].valid
        ) {
            return;
        }

        keyboardLedTxPending_[id->index] = false;

        if (length != 0) {
            keyboardLedApplied_[id->index] =
                keyboardLedInFlight_[id->index];
        }
    }


    void onBluetoothHidReady(
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

        oag::firmware::BluetoothHidDescriptorV2 parsed {};
        if (!bluetoothHidParser_.parseDescriptor(
                descriptor,
                descriptorLength,
                parsed
            )) {
            return;
        }

        oag::ProtocolKind protocol =
            oag::ProtocolKind::Unknown;

        if (parsed.hasGamepad) {
            protocol = oag::ProtocolKind::HidGamepad;
        } else if (parsed.hasKeyboard) {
            protocol = oag::ProtocolKind::HidKeyboard;
        } else if (parsed.hasMouse) {
            protocol = oag::ProtocolKind::HidMouse;
        }

        if (protocol == oag::ProtocolKind::Unknown) {
            return;
        }

        const auto id = registry_.connectBluetooth(
            handle,
            0,
            0,
            protocol
        );

        if (
            !id ||
            id->index >= oag::DeviceRegistry::kCapacity
        ) {
            return;
        }

        bluetoothHidDescriptors_[id->index] = parsed;

        if (parsed.hasGamepad) {
            const auto slot = slots_.bindFirstFree(*id);

            if (!slot) {
                if (!parsed.hasKeyboard && !parsed.hasMouse) {
                    bluetoothHidDescriptors_[id->index] = {};
                    registry_.disconnect(*id);
                    return;
                }
            } else {
                states_[*slot] = {};
                states_[*slot].source = *id;
                states_[*slot].connected = true;

                if (*slot < pendingRumbleValid_.size()) {
                    pendingRumble_[*slot] = {};
                    pendingRumbleValid_[*slot] = false;
                    bluetoothRumbleRetryNotBeforeUs_[*slot] = 0;
                }

                if (!primaryBluetoothGamepad_.valid()) {
                    primaryBluetoothGamepad_ = *id;
                }

                rebuildPcOutputRouting();
            }
        }

        if (parsed.hasKeyboard) {
            keyboardStates_[id->index] = {};
            keyboardStates_[id->index].source = *id;
            keyboardStates_[id->index].connected = true;
        }

        if (parsed.hasMouse) {
            mouseStates_[id->index] = {};
            mouseStates_[id->index].source = *id;
            mouseStates_[id->index].connected = true;
        }
    }

    void onBluetoothHidReport(
        oag::TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
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

        const auto& info =
            bluetoothHidDescriptors_[id->index];

        const std::uint64_t nowUs =
            time_us_64();

        bool composedChanged = false;

        if (info.hasGamepad) {
            const auto slot = slots_.slotFor(*id);

            if (
                slot &&
                *slot < states_.size() &&
                bluetoothHidParser_.parseGamepad(
                    *id,
                    transport,
                    info,
                    descriptor,
                    descriptorLength,
                    report,
                    reportLength,
                    nowUs,
                    states_[*slot]
                )
            ) {
                sendSlotOutput(*slot);
            }
        }

        if (
            info.hasKeyboard &&
            bluetoothHidParser_.parseKeyboard(
                *id,
                info,
                descriptor,
                descriptorLength,
                report,
                reportLength,
                nowUs,
                keyboardStates_[id->index]
            )
        ) {
            composedChanged = true;
        }

        if (
            info.hasMouse &&
            bluetoothHidParser_.parseMouse(
                *id,
                info,
                descriptor,
                descriptorLength,
                report,
                reportLength,
                nowUs,
                mouseStates_[id->index]
            )
        ) {
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
                    nowUs + kMouseAimHoldUs;
            }

            composedChanged = true;
        }

        if (composedChanged) {
            sendComposedOutput();
        }
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
                bluetoothRumbleRetryNotBeforeUs_[*slot] = 0;
            }

            slots_.release(*id);
        }

        const bool hadKeyboard =
            keyboardStates_[id->index].connected;
        const bool hadMouse =
            mouseStates_[id->index].connected;

        keyboardStates_[id->index] = {};
        mouseStates_[id->index] = {};
        bluetoothHidDescriptors_[id->index] = {};

        if (*id == primaryBluetoothGamepad_) {
            primaryBluetoothGamepad_ = {};
        }

        registry_.disconnect(*id);

        if (slot) {
            rebuildPcOutputRouting();
        }

        if (hadKeyboard || hadMouse) {
            currentMouseMotion_ = {};
            mouseAimActive_ = false;
            sendComposedOutput();
        }
    }

private:
    static constexpr std::uint8_t kRootCount = 3;
    static constexpr std::uint64_t kMouseAimHoldUs = 10000;
    static constexpr std::uint64_t kBluetoothRumbleRetryUs = 50000;
    static constexpr std::uint64_t kPrimarySelectHoldUs = 3000000ull;
    static constexpr std::uint64_t kKeyboardMouseModeHoldUs = 2000000ull;
    static constexpr std::uint8_t kModeToggleF4Usage = 0x3D;
    static constexpr std::uint8_t kModeToggleF5Usage = 0x3E;

    void serviceBluetoothHostV2() {
        const std::uint64_t nowUs =
            time_us_64();

        if (!bluetoothInitialized_) {
            if (
                bluetoothInitNotBeforeUs_ == 0 ||
                nowUs < bluetoothInitNotBeforeUs_
            ) {
                return;
            }

            // JoypadOS-derived coexistence lifecycle:
            // 1) Shared CYW43/L2CAP/SM core.
            // 2) BLE output owns the single ATT server + HIDS device.
            // 3) Bluetooth controller-input clients attach afterwards.
            // 4) HCI power-on is last.
            if (
                bluetoothHost_.initializeCore(*this) &&
                joypadBleOutput_.initialize(bluetoothHost_) &&
                bluetoothHost_.initializeInputProfiles() &&
                bluetoothHost_.startController()
            ) {
                bluetoothInitialized_ = true;
                bluetoothInitNotBeforeUs_ = 0;
                return;
            }

            bluetoothInitNotBeforeUs_ =
                nowUs + 1000000ull;
            return;
        }

        bluetoothHost_.poll();
        joypadBleOutput_.poll();
    }

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

        if (classification.hasQuirk(oag::UsbQuirkSonyButtonLayout)) {
            quirks.buttonLayout =
                oag::GenericHidButtonLayout::SonyPlayStation;
        } else if (
            classification.hasQuirk(oag::UsbQuirkModernButtonLayout)
        ) {
            quirks.buttonLayout =
                oag::GenericHidButtonLayout::ModernCanonical;
        }

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

            if (
                record == nullptr ||
                record->transport != oag::TransportType::UsbPioHost ||
                record->protocol != oag::ProtocolKind::HidKeyboard ||
                usbHidDescriptors_[i].valid
            ) {
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

    bool primarySelectionChordPressed(
        const oag::UniversalGamepadState& state
    ) const {
        return
            (state.buttons & oag::ButtonStart) != 0 &&
            (
                (state.buttons & oag::ButtonShare) != 0 ||
                (state.buttons & oag::ButtonBack) != 0 ||
                (state.buttons & oag::ButtonGuide) != 0
            );
    }

    void servicePrimaryControllerChords() {
        const std::uint64_t nowUs = time_us_64();

        for (std::size_t i = 0; i < states_.size(); ++i) {
            const oag::UniversalGamepadState& state = states_[i];

            if (
                !state.connected ||
                !state.source.valid() ||
                primaryChordSource_[i] != state.source
            ) {
                primaryChordSource_[i] =
                    state.connected ? state.source : oag::DeviceId {};
                primaryChordStartedUs_[i] = 0;
                primaryChordLatched_[i] = false;
            }

            if (!state.connected || !state.source.valid()) {
                continue;
            }

            if (!primarySelectionChordPressed(state)) {
                primaryChordStartedUs_[i] = 0;
                primaryChordLatched_[i] = false;
                continue;
            }

            if (primaryChordStartedUs_[i] == 0) {
                primaryChordStartedUs_[i] = nowUs;
                continue;
            }

            if (
                primaryChordLatched_[i] ||
                nowUs - primaryChordStartedUs_[i] < kPrimarySelectHoldUs
            ) {
                continue;
            }

            manualPrimaryGamepad_ = state.source;
            primaryChordLatched_[i] = true;
            rebuildPcOutputRouting();
            break;
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

    bool isRoutableGamepad(oag::DeviceId device) const {
        const oag::DeviceRecord* record = registry_.find(device);

        if (record == nullptr || !record->connected) {
            return false;
        }

        return
            record->protocol == oag::ProtocolKind::HidGamepad ||
            record->protocol == oag::ProtocolKind::XusbXbox360 ||
            record->protocol == oag::ProtocolKind::XgipXboxOne;
    }

    bool isBluetoothGamepad(oag::DeviceId device) const {
        const oag::DeviceRecord* record = registry_.find(device);

        return
            record != nullptr &&
            record->connected &&
            record->protocol == oag::ProtocolKind::HidGamepad &&
            (
                record->transport == oag::TransportType::BluetoothLe ||
                record->transport == oag::TransportType::BluetoothClassic
            );
    }

    void servicePlatformPlayerAssignments() {
        std::uint8_t receiverSlot = 0;
        std::uint8_t playerIndex = 0;
        bool primaryOutputChanged = false;

        while (platformOutput_.takePlayerAssignment(
                receiverSlot,
                playerIndex
            )) {
            if (
                receiverSlot >= pcOutputRoutes_.size() ||
                playerIndex >= pcOutputRoutes_.size()
            ) {
                continue;
            }

            // Windows/xusb22 assigns XInput user numbers dynamically for
            // receiver child controllers. Player 1 is XInput user index 0.
            if (
                playerIndex == 0 &&
                hostPrimaryOutputSlot_ != receiverSlot
            ) {
                hostPrimaryOutputSlot_ = receiverSlot;
                primaryOutputChanged = true;
            }
        }

        if (primaryOutputChanged) {
            rebuildPcOutputRouting();
        }
    }

    void rebuildPcOutputRouting() {
        std::array<
            std::optional<oag::LogicalSlotId>,
            oag::firmware::PcXinputDevice::kOutputSlots
        > nextRoutes {};

        if (hostPrimaryOutputSlot_ >= nextRoutes.size()) {
            hostPrimaryOutputSlot_ = 0;
        }

        const auto alreadyRouted =
            [&](oag::LogicalSlotId slot) {
                for (const auto& route : nextRoutes) {
                    if (route && *route == slot) {
                        return true;
                    }
                }
                return false;
            };

        const auto appendRemaining =
            [&](oag::LogicalSlotId slot) {
                if (alreadyRouted(slot)) {
                    return;
                }

                for (std::size_t output = 0;
                     output < nextRoutes.size();
                     ++output) {
                    if (!nextRoutes[output]) {
                        nextRoutes[output] = slot;
                        return;
                    }
                }
            };

        std::optional<oag::LogicalSlotId> primarySlot;

        // Manual Start + Share/View/Guide selection overrides automatic
        // Bluetooth priority while that physical controller stays connected.
        if (
            manualPrimaryGamepad_.valid() &&
            isRoutableGamepad(manualPrimaryGamepad_)
        ) {
            primarySlot = slots_.slotFor(manualPrimaryGamepad_);
        } else {
            manualPrimaryGamepad_ = {};
        }

        // Without a manual override, keep the first connected Bluetooth
        // gamepad as the preferred physical primary.
        if (!primarySlot) {
            if (
                primaryBluetoothGamepad_.valid() &&
                isBluetoothGamepad(primaryBluetoothGamepad_)
            ) {
                primarySlot =
                    slots_.slotFor(primaryBluetoothGamepad_);
            } else {
                primaryBluetoothGamepad_ = {};

                for (std::size_t i = 0;
                     i < oag::LogicalSlotManager::kGamepadSlots;
                     ++i) {
                    const auto slot =
                        static_cast<oag::LogicalSlotId>(i);
                    const oag::DeviceId device =
                        slots_.deviceFor(slot);

                    if (!isBluetoothGamepad(device)) {
                        continue;
                    }

                    primaryBluetoothGamepad_ = device;
                    primarySlot = slot;
                    break;
                }
            }
        }

        // If no Bluetooth gamepad exists, the first routable wired gamepad is
        // primary, but it still occupies the host's actual Player-1 receiver
        // child rather than an assumed receiver slot.
        if (!primarySlot) {
            for (std::size_t i = 0;
                 i < oag::LogicalSlotManager::kGamepadSlots;
                 ++i) {
                const auto slot =
                    static_cast<oag::LogicalSlotId>(i);
                const oag::DeviceId device =
                    slots_.deviceFor(slot);

                if (!isRoutableGamepad(device)) {
                    continue;
                }

                primarySlot = slot;
                break;
            }
        }

        if (primarySlot) {
            nextRoutes[hostPrimaryOutputSlot_] = *primarySlot;
        }

        // Remaining Bluetooth gamepads retain priority over wired gamepads.
        for (std::size_t i = 0;
             i < oag::LogicalSlotManager::kGamepadSlots;
             ++i) {
            const auto slot = static_cast<oag::LogicalSlotId>(i);
            const oag::DeviceId device = slots_.deviceFor(slot);

            if (isBluetoothGamepad(device)) {
                appendRemaining(slot);
            }
        }

        // Wired gamepads fill every remaining receiver child.
        for (std::size_t i = 0;
             i < oag::LogicalSlotManager::kGamepadSlots;
             ++i) {
            const auto slot = static_cast<oag::LogicalSlotId>(i);
            const oag::DeviceId device = slots_.deviceFor(slot);

            if (
                isRoutableGamepad(device) &&
                !isBluetoothGamepad(device)
            ) {
                appendRemaining(slot);
            }
        }

        pcOutputRoutes_ = nextRoutes;

        for (std::size_t i = 0; i < pendingRumble_.size(); ++i) {
            pendingRumble_[i] = {};
            pendingRumbleValid_[i] = false;
            bluetoothRumbleRetryNotBeforeUs_[i] = 0;
        }

        sendComposedOutput();

        for (std::size_t pcSlot = 0;
             pcSlot < pcOutputRoutes_.size();
             ++pcSlot) {
            if (pcSlot == hostPrimaryOutputSlot_) {
                continue;
            }

            oag::LogicalGamepadState output {};

            if (pcOutputRoutes_[pcSlot]) {
                const oag::LogicalSlotId internalSlot =
                    *pcOutputRoutes_[pcSlot];

                if (
                    internalSlot < states_.size() &&
                    states_[internalSlot].connected
                ) {
                    output = mapping_.process(states_[internalSlot]);
                }
            }

            platformOutput_.submit(
                static_cast<std::uint8_t>(pcSlot),
                output
            );
        }
    }

    oag::LogicalGamepadState basePrimaryOutput() const {
        if (
            hostPrimaryOutputSlot_ >= pcOutputRoutes_.size() ||
            !pcOutputRoutes_[hostPrimaryOutputSlot_]
        ) {
            return {};
        }

        const oag::LogicalSlotId slot =
            *pcOutputRoutes_[hostPrimaryOutputSlot_];

        if (slot >= states_.size() || !states_[slot].connected) {
            return {};
        }

        return mapping_.process(states_[slot]);
    }

    void sendSlotOutput(oag::LogicalSlotId slot) {
        if (slot >= states_.size()) {
            return;
        }

        for (std::size_t pcSlot = 0;
             pcSlot < pcOutputRoutes_.size();
             ++pcSlot) {
            if (
                !pcOutputRoutes_[pcSlot] ||
                *pcOutputRoutes_[pcSlot] != slot
            ) {
                continue;
            }

            if (pcSlot == hostPrimaryOutputSlot_) {
                sendComposedOutput();
                return;
            }

            oag::LogicalGamepadState output {};

            if (states_[slot].connected) {
                output = mapping_.process(states_[slot]);
            }

            platformOutput_.submit(
                static_cast<std::uint8_t>(pcSlot),
                output
            );
            return;
        }
    }

    void sendComposedOutput() {
        oag::KeyboardState keyboard = combinedKeyboard();
        const oag::MouseState mouse = combinedMouse();

        const bool hasKeyboard = keyboard.connected;
        const bool hasMouse = mouse.connected;

        // In Native mode K/M never create or modify the XInput player.
        // Physical gamepads keep their normal route while K/M are forwarded
        // through the standard HID keyboard/mouse interfaces.
        if (keyboardMouseMode_ == KeyboardMouseOutputMode::Native) {
            const oag::LogicalGamepadState output =
                basePrimaryOutput();

            platformOutput_.submit(
                hostPrimaryOutputSlot_,
                output
            );
            joypadBleOutput_.submit(output);
            return;
        }

        // F4+F5 is a reserved global system chord. Even in Controller mode,
        // the chord itself must never leak into any mapping or combo.
        if (
            keyboard.pressed(kModeToggleF4Usage) &&
            keyboard.pressed(kModeToggleF5Usage)
        ) {
            keyboard.setPressed(kModeToggleF4Usage, false);
            keyboard.setPressed(kModeToggleF5Usage, false);
        }

        oag::LogicalGamepadState output =
            keyboardMouse_.apply(
                hasKeyboard ? &keyboard : nullptr,
                hasMouse ? &mouse : nullptr,
                mouseAimActive_
                    ? currentMouseMotion_
                    : oag::MouseMotion {},
                basePrimaryOutput()
            );

        if (!output.connected && !hasKeyboard && !hasMouse) {
            const oag::LogicalGamepadState neutral {};

            platformOutput_.submit(
                hostPrimaryOutputSlot_,
                neutral
            );
            joypadBleOutput_.submit(neutral);
            return;
        }

        platformOutput_.submit(hostPrimaryOutputSlot_, output);
        joypadBleOutput_.submit(output);
    }

    void serviceKeyboardMouseModeToggle() {
        const oag::KeyboardState keyboard = combinedKeyboard();
        const bool chordDown =
            keyboard.pressed(kModeToggleF4Usage) &&
            keyboard.pressed(kModeToggleF5Usage);

        if (!chordDown) {
            keyboardMouseModeChordStartedUs_ = 0;
            keyboardMouseModeChordLatched_ = false;
            return;
        }

        const std::uint64_t nowUs = time_us_64();

        if (keyboardMouseModeChordStartedUs_ == 0) {
            keyboardMouseModeChordStartedUs_ = nowUs;
            return;
        }

        if (
            keyboardMouseModeChordLatched_ ||
            nowUs - keyboardMouseModeChordStartedUs_ <
                kKeyboardMouseModeHoldUs
        ) {
            return;
        }

        keyboardMouseMode_ =
            keyboardMouseMode_ == KeyboardMouseOutputMode::Native
                ? KeyboardMouseOutputMode::Controller
                : KeyboardMouseOutputMode::Native;

        keyboardMouseModeChordLatched_ = true;
        currentMouseMotion_ = {};
        currentNativeWheel_ = 0;
        currentNativePan_ = 0;
        mouseAimActive_ = false;
        mouseAimExpiresUs_ = 0;

        if (keyboardMouseMode_ == KeyboardMouseOutputMode::Controller) {
            nativeKmOutput_.releaseAll();
        }

        sendComposedOutput();
    }

    void serviceNativeKeyboardMouseOutput() {
        const std::uint64_t nowUs = time_us_64();

        if (keyboardMouseMode_ != KeyboardMouseOutputMode::Native) {
            nativeKmOutput_.setEnabled(false);
            nativeKmOutput_.task(nowUs);
            return;
        }

        oag::KeyboardState keyboard = combinedKeyboard();
        oag::MouseState mouse = combinedMouse();

        // Never expose the reserved F4+F5 chord to the target host.
        if (
            keyboard.pressed(kModeToggleF4Usage) &&
            keyboard.pressed(kModeToggleF5Usage)
        ) {
            keyboard.setPressed(kModeToggleF4Usage, false);
            keyboard.setPressed(kModeToggleF5Usage, false);
        }

        const oag::NativeKmComboFrame frame =
            nativeKmCombos_.apply(keyboard, mouse);

        nativeKmOutput_.setEnabled(true);
        nativeKmOutput_.updateState(frame.keyboard, frame.mouse);

        if (
            currentMouseMotion_.dx != 0 ||
            currentMouseMotion_.dy != 0 ||
            currentNativeWheel_ != 0 ||
            currentNativePan_ != 0
        ) {
            nativeKmOutput_.addMouseMotion(
                currentMouseMotion_.dx,
                currentMouseMotion_.dy,
                currentNativeWheel_,
                currentNativePan_
            );

            currentMouseMotion_ = {};
            currentNativeWheel_ = 0;
            currentNativePan_ = 0;
            mouseAimActive_ = false;
            mouseAimExpiresUs_ = 0;
        }

        nativeKmOutput_.task(nowUs);
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

            if (!pcOutputRoutes_[i]) {
                pendingRumbleValid_[i] = false;
                continue;
            }

            const oag::LogicalSlotId slot = *pcOutputRoutes_[i];
            const oag::DeviceId source = slots_.deviceFor(slot);
            const oag::DeviceRecord* record = registry_.find(source);

            if (record == nullptr) {
                pendingRumbleValid_[i] = false;
                continue;
            }

            if (
                record->transport == oag::TransportType::BluetoothLe &&
                record->protocol == oag::ProtocolKind::HidGamepad &&
                source.index < bluetoothHidDescriptors_.size() &&
                bluetoothHidDescriptors_[source.index].profile ==
                    oag::firmware::BluetoothGamepadProfileV2::XboxBle
            ) {
                const std::uint64_t nowUs = time_us_64();

                if (nowUs < bluetoothRumbleRetryNotBeforeUs_[i]) {
                    continue;
                }

                const auto scaleToPercent =
                    [](std::uint8_t value) -> std::uint8_t {
                        return static_cast<std::uint8_t>(
                            (
                                static_cast<std::uint16_t>(value) *
                                100u
                            ) /
                            255u
                        );
                    };

                std::uint8_t rumblePacket[8] {};

                if (
                    pendingRumble_[i].leftMotor == 0 &&
                    pendingRumble_[i].rightMotor == 0
                ) {
                    // Enable all actuator fields and send zero magnitudes to
                    // guarantee a stop, matching the historical working path.
                    rumblePacket[0] = 0x0F;
                } else {
                    std::uint8_t actuatorMask = 0;

                    if (pendingRumble_[i].rightMotor != 0) {
                        actuatorMask |= 0x01; // weak motor
                    }

                    if (pendingRumble_[i].leftMotor != 0) {
                        actuatorMask |= 0x02; // strong motor
                    }

                    rumblePacket[0] = actuatorMask;
                    rumblePacket[3] =
                        scaleToPercent(pendingRumble_[i].leftMotor);
                    rumblePacket[4] =
                        scaleToPercent(pendingRumble_[i].rightMotor);
                    rumblePacket[5] = 0xFF;
                    rumblePacket[6] = 0x00;
                    rumblePacket[7] = 25;
                }

                const auto result =
                    bluetoothHost_.sendLeOutputReport(
                        record->bluetooth.connectionHandle,
                        record->bluetooth.serviceInstance,
                        0x03,
                        rumblePacket,
                        sizeof(rumblePacket)
                    );

                if (
                    result ==
                    oag::firmware::BluetoothHidOutputResult::Accepted
                ) {
                    pendingRumbleValid_[i] = false;
                    bluetoothRumbleRetryNotBeforeUs_[i] = 0;
                } else if (
                    result ==
                    oag::firmware::BluetoothHidOutputResult::Busy
                ) {
                    bluetoothRumbleRetryNotBeforeUs_[i] =
                        nowUs + kBluetoothRumbleRetryUs;
                } else {
                    pendingRumbleValid_[i] = false;
                    bluetoothRumbleRetryNotBeforeUs_[i] = 0;
                }

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
                if (slot >= xgipPhases_.size() ||
                    xgipPhases_[slot] != XgipInitPhase::Ready ||
                    xgipTxPending_[slot]) {
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

    oag::firmware::BluetoothHostV2 bluetoothHost_;
    oag::firmware::JoypadBleOutput joypadBleOutput_;
    oag::firmware::BluetoothHidParserV2 bluetoothHidParser_;
    bool bluetoothInitialized_ = false;
    std::uint64_t bluetoothInitNotBeforeUs_ = 0;

    oag::DeviceRegistry registry_;
    oag::LogicalSlotManager slots_;
    oag::XusbInputDriver xusb_;
    oag::XgipInputDriver xgip_;
    oag::BootKeyboardInputDriver keyboard_;
    oag::BootMouseInputDriver mouse_;
    oag::GenericHidGamepadDriver genericHid_;
    oag::PassThroughMapping mapping_;
    oag::KeyboardMouseGamepadMapper keyboardMouse_;
    oag::NativeKmComboEngine nativeKmCombos_;
    oag::firmware::PcXinputPlatformDriver platformOutput_;
    oag::firmware::PcNativeKmOutput nativeKmOutput_;

    std::array<
        std::optional<oag::LogicalSlotId>,
        oag::firmware::PcXinputDevice::kOutputSlots
    > pcOutputRoutes_ {};

    oag::DeviceId primaryBluetoothGamepad_ {};
    oag::DeviceId manualPrimaryGamepad_ {};

    KeyboardMouseOutputMode keyboardMouseMode_ =
        KeyboardMouseOutputMode::Native;
    std::uint64_t keyboardMouseModeChordStartedUs_ = 0;
    bool keyboardMouseModeChordLatched_ = false;

    std::int16_t currentNativeWheel_ = 0;
    std::int16_t currentNativePan_ = 0;

    // Receiver child currently assigned by Windows/xusb22 to XInput Player 1.
    std::uint8_t hostPrimaryOutputSlot_ = 0;

    std::array<
        oag::DeviceId,
        oag::LogicalSlotManager::kGamepadSlots
    > primaryChordSource_ {};

    std::array<
        std::uint64_t,
        oag::LogicalSlotManager::kGamepadSlots
    > primaryChordStartedUs_ {};

    std::array<
        bool,
        oag::LogicalSlotManager::kGamepadSlots
    > primaryChordLatched_ {};

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

    static constexpr std::size_t kMaxUsbHidDescriptorBytes = 1024;

    std::array<
        oag::firmware::BluetoothHidDescriptorV2,
        oag::DeviceRegistry::kCapacity
    > usbHidDescriptors_ {};

    std::array<
        std::array<std::uint8_t, kMaxUsbHidDescriptorBytes>,
        oag::DeviceRegistry::kCapacity
    > usbHidRawDescriptors_ {};

    std::array<
        std::uint16_t,
        oag::DeviceRegistry::kCapacity
    > usbHidRawDescriptorLengths_ {};

    std::array<
        oag::firmware::BluetoothHidDescriptorV2,
        oag::DeviceRegistry::kCapacity
    > bluetoothHidDescriptors_ {};

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

    std::array<
        std::uint64_t,
        oag::firmware::PcXinputDevice::kOutputSlots
    > bluetoothRumbleRetryNotBeforeUs_ {};
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
