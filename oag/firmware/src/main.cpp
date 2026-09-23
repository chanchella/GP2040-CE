#include <array>
#include <cstdint>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"

#include "oag/device/device_registry.h"
#include "oag/firmware/pc_hid_output.h"
#include "oag/firmware/usb_pio_host.h"
#include "oag/firmware/xinput_host.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/pass_through_mapping.h"
#include "oag/protocol/xusb/xusb_input_driver.h"

namespace {

class FirmwareCore {
public:
    bool start() {
        if (!tud_init(0)) {
            return false;
        }

        // U1 has Bluetooth disabled. The product invariant remains:
        // PIO USB Host must be initialized before any future CYW43/BT start.
        return usbHost_.start();
    }

    void task() {
        tud_task();
        usbHost_.task();
    }

    void onXusbMounted(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        std::uint16_t vid = 0;
        std::uint16_t pid = 0;

        if (!tuh_vid_pid_get(devAddr, &vid, &pid)) {
            return;
        }

        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.connectUsb(
            handle,
            vid,
            pid,
            oag::ProtocolKind::XusbXbox360
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
    }

    void onXusbUnmounted(
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

        const auto slot = slots_.slotFor(*id);

        if (slot) {
            states_[*slot] = {};

            if (*slot == 0) {
                pcOutput_.sendNeutral();
            }
        }

        slots_.release(*id);
        registry_.disconnect(*id);
    }

    void onXusbReport(
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

        const auto slot = slots_.slotFor(*id);
        if (!slot || *slot >= states_.size()) {
            return;
        }

        if (!xusb_.parse(
                *id,
                report,
                length,
                time_us_64(),
                states_[*slot]
            )) {
            return;
        }

        // U1-HW1 exposes one PC HID gamepad while keeping the internal
        // slot model independent and four-wide.
        if (*slot == 0) {
            const oag::LogicalGamepadState logical =
                mapping_.process(states_[*slot]);

            pcOutput_.send(logical);
        }
    }

private:
    oag::firmware::UsbPioHost usbHost_;
    oag::DeviceRegistry registry_;
    oag::LogicalSlotManager slots_;
    oag::XusbInputDriver xusb_;
    oag::PassThroughMapping mapping_;
    oag::firmware::PcHidOutput pcOutput_;
    std::array<
        oag::UniversalGamepadState,
        oag::LogicalSlotManager::kGamepadSlots
    > states_ {};
};

FirmwareCore gCore;

} // namespace

extern "C" void tuh_xinput_mount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t type,
    std::uint8_t subtype
) {
    (void)type;
    (void)subtype;
    gCore.onXusbMounted(dev_addr, instance);
}

extern "C" void tuh_xinput_umount_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    gCore.onXusbUnmounted(dev_addr, instance);
}

extern "C" void tuh_xinput_report_received_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    gCore.onXusbReport(dev_addr, instance, report, len);
}

extern "C" void tuh_xinput_report_sent_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    (void)dev_addr;
    (void)instance;
    (void)report;
    (void)len;
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
