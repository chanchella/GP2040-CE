#include <array>
#include <cstdint>
#include <optional>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include "oag/device/device_registry.h"
#include "oag/firmware/pc_hid_output.h"
#include "oag/firmware/usb_pio_host.h"
#include "oag/firmware/xinput_host.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/pass_through_mapping.h"
#include "oag/protocol/xusb/xusb_input_driver.h"
#include "oag/transport/host_root_reconciler.h"

namespace {

class FirmwareCore {
public:
    bool start() {
        if (!tud_init(0)) {
            return false;
        }

        // U1 has Bluetooth disabled. The product invariant remains:
        // PIO USB Host must be initialized before any future CYW43/BT start.
        if (!usbHost_.start()) {
            return false;
        }

        nextHostHealthCheckUs_ = time_us_64() + kHostHealthPeriodUs;
        return true;
    }

    void task() {
        tud_task();
        usbHost_.task();
        serviceHostHealthWatchdog();
    }

    void onXusbMounted(
        std::uint8_t devAddr,
        std::uint8_t instance
    ) {
        rememberMountedRoot(devAddr);

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
        forgetMountedRoot(devAddr);

        const oag::UsbTransportHandle handle {
            devAddr,
            instance,
        };

        const auto id = registry_.findUsb(handle);
        if (!id) {
            return;
        }

        const auto primaryBefore = primaryPcSlot();
        const auto slot = slots_.slotFor(*id);

        if (slot) {
            states_[*slot] = {};
        }

        slots_.release(*id);
        registry_.disconnect(*id);

        if (slot && primaryBefore && *slot == *primaryBefore) {
            sendPrimaryPcOutput();
        }
    }

    void onUsbDeviceUnmounted(std::uint8_t devAddr) {
        forgetMountedRoot(devAddr);

        // TinyUSB calls the generic device-unmount callback before closing
        // class drivers. Clean every possible U1 XUSB interface here as a
        // transport-level safety net; the later class callback is idempotent.
        for (std::uint8_t instance = 0; instance < CFG_TUH_XINPUT; ++instance) {
            onXusbUnmounted(devAddr, instance);
        }
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

        const auto primary = primaryPcSlot();
        if (primary && *slot == *primary) {
            pcOutput_.send(mapping_.process(states_[*slot]));
        }
    }

private:
    static constexpr std::uint64_t kHostHealthPeriodUs = 2000000;
    static constexpr std::uint8_t kRootCount = 3;

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

    void serviceHostHealthWatchdog() {
        const std::uint64_t now = time_us_64();
        if (now < nextHostHealthCheckUs_) {
            return;
        }

        nextHostHealthCheckUs_ = now + kHostHealthPeriodUs;

        const std::uint8_t physicalMask = usbHost_.physicalRootMask();
        const auto plan = oag::planHostRootReconcile(
            physicalMask,
            mountedRootMask_
        );

        if (!plan.healthy()) {
            usbHost_.reconcileRootEvents(
                plan.removeMask,
                plan.attachMask
            );
        }
    }

    std::optional<oag::LogicalSlotId> primaryPcSlot() const {
        for (std::size_t i = 0; i < oag::LogicalSlotManager::kGamepadSlots; ++i) {
            const auto slot = static_cast<oag::LogicalSlotId>(i);
            if (slots_.deviceFor(slot).valid()) {
                return slot;
            }
        }

        return std::nullopt;
    }

    void sendPrimaryPcOutput() {
        const auto primary = primaryPcSlot();

        if (!primary ||
            *primary >= states_.size() ||
            !states_[*primary].connected) {
            pcOutput_.sendNeutral();
            return;
        }

        pcOutput_.send(mapping_.process(states_[*primary]));
    }

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

    std::array<
        std::uint8_t,
        CFG_TUH_DEVICE_MAX + 1
    > rootByDevice_ {};

    std::uint8_t mountedRootMask_ = 0;
    std::uint64_t nextHostHealthCheckUs_ = 0;
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

extern "C" void tuh_umount_cb(std::uint8_t dev_addr) {
    gCore.onUsbDeviceUnmounted(dev_addr);
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
