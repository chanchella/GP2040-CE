#include "oag/firmware/usb_pio_host.h"

#include <cstdint>
#include <cstring>

#include "hardware/sync.h"
#include "pico/time.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "tusb.h"
#include "host/hcd.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "oag/firmware/xinput_host.h"

namespace oag::firmware {
namespace {

constexpr std::uint8_t kHostRhPort = 1;
constexpr std::uint8_t kPort1Dp = 2;
constexpr std::uint8_t kPort2Dp = 4;
constexpr std::uint8_t kPort3Dp = 6;
constexpr std::uint8_t kRootCount = 3;

bool rootLinePresent(root_port_t* root) {
    if (root == nullptr || !root->initialized) {
        return false;
    }

    const port_pin_status_t state = pio_usb_bus_get_line_state(root);
    return state == PORT_PIN_FS_IDLE || state == PORT_PIN_LS_IDLE;
}

} // namespace

bool UsbPioHost::start() {
    if (ready_) {
        return true;
    }

    static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.pin_dp = kPort1Dp;
    config.pinout = PIO_USB_PINOUT_DPDM;

    if (!tuh_configure(
            kHostRhPort,
            TUH_CFGID_RPI_PIO_USB_CONFIGURATION,
            &config
        )) {
        return false;
    }

    if (!tuh_init(kHostRhPort)) {
        return false;
    }

    const int port2 = pio_usb_host_add_port(kPort2Dp, PIO_USB_PINOUT_DPDM);
    const int port3 = pio_usb_host_add_port(kPort3Dp, PIO_USB_PINOUT_DPDM);

    if (port2 != 0 || port3 != 0) {
        tuh_deinit(kHostRhPort);
        return false;
    }

    sleep_us(10);
    ready_ = true;
    return true;
}

void UsbPioHost::task() {
    if (ready_) {
        tuh_task();
    }
}

void UsbPioHost::stop() {
    if (!ready_) {
        return;
    }

    tuh_deinit(kHostRhPort);
    ready_ = false;
}

std::uint8_t UsbPioHost::physicalRootMask() const {
    std::uint8_t mask = 0;

    for (std::uint8_t rootIndex = 0; rootIndex < kRootCount; ++rootIndex) {
        root_port_t* root = PIO_USB_ROOT_PORT(rootIndex);
        if (rootLinePresent(root)) {
            mask |= static_cast<std::uint8_t>(1u << rootIndex);
        }
    }

    return mask;
}

void UsbPioHost::forceReenumerateConnectedRoots() {
    if (!ready_) {
        return;
    }

    const std::uint8_t presentMask = physicalRootMask();

    // Freeze the PIO-USB SOF/timer ISR while resetting the shared root/endpoint
    // runtime bookkeeping. This is intentionally a HOST-side recovery only;
    // TinyUSB device mode (the OAG connection to Windows) is untouched.
    const std::uint32_t irqState = save_and_disable_interrupts();

    for (std::uint8_t rootIndex = 0; rootIndex < kRootCount; ++rootIndex) {
        root_port_t* root = PIO_USB_ROOT_PORT(rootIndex);
        if (root == nullptr || !root->initialized) {
            continue;
        }

        for (std::uint8_t epIndex = 0; epIndex < PIO_USB_EP_POOL_CNT; ++epIndex) {
            endpoint_t* ep = PIO_USB_ENDPOINT(epIndex);
            if (ep->size != 0 && ep->root_idx == rootIndex) {
                std::memset(ep, 0, sizeof(*ep));
            }
        }

        root->addr0_exists = false;
        root->root_device = nullptr;
        root->ep_complete = 0;
        root->ep_error = 0;
        root->ep_stalled = 0;
        root->ints = 0;
        root->event = EVENT_NONE;

        const bool present =
            (presentMask & static_cast<std::uint8_t>(1u << rootIndex)) != 0;

        root->connected = present;
        root->suspended = present;
        if (present) {
            const port_pin_status_t state = pio_usb_bus_get_line_state(root);
            root->is_fullspeed = state == PORT_PIN_FS_IDLE;
        }
    }

    restore_interrupts(irqState);

    // Queue REMOVE first for every root so TinyUSB releases any stale logical
    // device/address state. Then re-issue ATTACH only for roots that physically
    // contain a device. The queue preserves this order.
    for (std::uint8_t rootIndex = 0; rootIndex < kRootCount; ++rootIndex) {
        root_port_t* root = PIO_USB_ROOT_PORT(rootIndex);
        if (root != nullptr && root->initialized) {
            hcd_event_device_remove(
                static_cast<std::uint8_t>(rootIndex + 1u),
                false
            );
        }
    }

    for (std::uint8_t rootIndex = 0; rootIndex < kRootCount; ++rootIndex) {
        const bool present =
            (presentMask & static_cast<std::uint8_t>(1u << rootIndex)) != 0;

        if (present) {
            hcd_event_device_attach(
                static_cast<std::uint8_t>(rootIndex + 1u),
                false
            );
        }
    }
}

} // namespace oag::firmware

extern "C" usbh_class_driver_t const* usbh_app_driver_get_cb(
    std::uint8_t* driver_count
) {
    static usbh_class_driver_t drivers[] = {
        {
#if CFG_TUSB_DEBUG >= 2
            .name = "OAG_XUSB",
#endif
            .init = xinputh_init,
            .open = xinputh_open,
            .set_config = xinputh_set_config,
            .xfer_cb = xinputh_xfer_cb,
            .close = xinputh_close,
        },
    };

    *driver_count = 1;
    return drivers;
}

extern "C" void tuh_hid_report_received_cb(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    // TinyUSB HID host is enabled only as a compile anchor in U1.
    // Generic HID input routing starts in U2; intentionally discard here.
    (void)dev_addr;
    (void)instance;
    (void)report;
    (void)len;
}
