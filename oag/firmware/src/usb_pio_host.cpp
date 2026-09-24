#include "oag/firmware/usb_pio_host.h"

#include <cstdint>

#include "pico/time.h"
#include "pio_usb.h"
#include "tusb.h"
#include "host/hcd.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "oag/firmware/usb_pio_probe.h"
#include "oag/firmware/xinput_host.h"

namespace oag::firmware {
namespace {

constexpr std::uint8_t kHostRhPort = 1;
constexpr std::uint8_t kPort1Dp = 2;
constexpr std::uint8_t kPort2Dp = 4;
constexpr std::uint8_t kPort3Dp = 6;
constexpr std::uint8_t kRootCount = 3;

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
    return ready_ ? oag_pio_physical_root_mask() : 0;
}

void UsbPioHost::reconcileRootEvents(
    std::uint8_t removeMask,
    std::uint8_t attachMask
) {
    if (!ready_) {
        return;
    }

    constexpr std::uint8_t validMask =
        static_cast<std::uint8_t>((1u << kRootCount) - 1u);

    removeMask &= validMask;
    attachMask &= validMask;

    for (std::uint8_t rootIndex = 0;
         rootIndex < kRootCount;
         ++rootIndex) {
        const std::uint8_t bit =
            static_cast<std::uint8_t>(1u << rootIndex);

        if ((removeMask & bit) != 0) {
            hcd_event_device_remove(
                static_cast<std::uint8_t>(rootIndex + 1u),
                false
            );
        }
    }

    for (std::uint8_t rootIndex = 0;
         rootIndex < kRootCount;
         ++rootIndex) {
        const std::uint8_t bit =
            static_cast<std::uint8_t>(1u << rootIndex);

        if ((attachMask & bit) != 0) {
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
