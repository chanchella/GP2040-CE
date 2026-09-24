/*
 * SPDX-License-Identifier: MIT
 *
 * U1 XUSB host adapter derived from the MIT-licensed GP2040-CE
 * XInput host implementation in the G2E3 Golden source.
 */

#include "oag/firmware/xinput_host.h"

#if (CFG_TUH_ENABLED && CFG_TUH_XINPUT)

#include <cstring>

#include "host/usbh_pvt.h"

namespace {

struct XinputInterface {
    std::uint8_t itfNum = 0;
    std::uint8_t epIn = 0;
    std::uint8_t epOut = 0;
    std::uint8_t type = OAG_XINPUT_UNKNOWN;
    std::uint8_t subtype = 0;
    bool gameplay = false;

    std::uint16_t epInSize = 0;
    std::uint16_t epOutSize = 0;

    std::uint8_t epInBuffer[CFG_TUH_XINPUT_EPIN_BUFSIZE] {};
    std::uint8_t epOutBuffer[CFG_TUH_XINPUT_EPOUT_BUFSIZE] {};
};

struct XinputDevice {
    std::uint8_t instanceCount = 0;
    XinputInterface instances[CFG_TUH_XINPUT] {};
};

XinputDevice gDevices[CFG_TUH_DEVICE_MAX] {};

constexpr std::uint8_t kReservedDescriptorType = 0x21;

XinputDevice* getDevice(std::uint8_t devAddr) {
    if (devAddr == 0 || devAddr > CFG_TUH_DEVICE_MAX) {
        return nullptr;
    }
    return &gDevices[devAddr - 1];
}

XinputInterface* getInstance(std::uint8_t devAddr, std::uint8_t instance) {
    XinputDevice* device = getDevice(devAddr);
    if (device == nullptr || instance >= CFG_TUH_XINPUT) {
        return nullptr;
    }
    return &device->instances[instance];
}

std::uint8_t findInstanceByInterface(
    std::uint8_t devAddr,
    std::uint8_t interfaceNumber
) {
    for (std::uint8_t i = 0; i < CFG_TUH_XINPUT; ++i) {
        XinputInterface* instance = getInstance(devAddr, i);
        if (instance != nullptr &&
            (instance->epIn != 0 || instance->epOut != 0) &&
            instance->itfNum == interfaceNumber) {
            return i;
        }
    }
    return 0xFF;
}

std::uint8_t findInstanceByEndpoint(
    std::uint8_t devAddr,
    std::uint8_t epAddr
) {
    for (std::uint8_t i = 0; i < CFG_TUH_XINPUT; ++i) {
        XinputInterface* instance = getInstance(devAddr, i);
        if (instance != nullptr &&
            (instance->epIn == epAddr || instance->epOut == epAddr)) {
            return i;
        }
    }
    return 0xFF;
}

void completeMount(std::uint8_t devAddr, std::uint8_t instanceIndex) {
    XinputInterface* instance = getInstance(devAddr, instanceIndex);
    if (instance == nullptr) {
        return;
    }

    tuh_xinput_mount_cb(
        devAddr,
        instanceIndex,
        instance->type,
        instance->subtype
    );

    // Preserve the G2E3/T29 startup behavior. Some compatible wired
    // controllers remain silent until they see normal OUT traffic.
    if (instance->gameplay &&
        instance->epOut != 0 &&
        instance->epOutSize >= 3 &&
        !usbh_edpt_busy(devAddr, instance->epOut) &&
        usbh_edpt_claim(devAddr, instance->epOut)) {

        static constexpr std::uint8_t kPlayer1Led[3] = {
            0x01, 0x03, 0x06
        };

        std::memcpy(
            instance->epOutBuffer,
            kPlayer1Led,
            sizeof(kPlayer1Led)
        );

        if (!usbh_edpt_xfer(
                devAddr,
                instance->epOut,
                instance->epOutBuffer,
                sizeof(kPlayer1Led)
            )) {
            usbh_edpt_release(devAddr, instance->epOut);
        }
    }

    if (instance->gameplay) {
        tuh_xinput_receive_report(devAddr, instanceIndex);
    }

    usbh_driver_set_config_complete(devAddr, instance->itfNum);
}

} // namespace

extern "C" std::uint8_t tuh_xinput_instance_count(std::uint8_t dev_addr) {
    XinputDevice* device = getDevice(dev_addr);
    return device == nullptr ? 0 : device->instanceCount;
}

extern "C" bool tuh_xinput_mounted(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    XinputInterface* iface = getInstance(dev_addr, instance);
    return iface != nullptr && (iface->epIn != 0 || iface->epOut != 0);
}

extern "C" bool tuh_xinput_ready(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    XinputInterface* iface = getInstance(dev_addr, instance);

    return iface != nullptr &&
        iface->gameplay &&
        iface->epIn != 0 &&
        iface->epInSize != 0 &&
        tuh_ready(dev_addr) &&
        !usbh_edpt_busy(dev_addr, iface->epIn);
}

extern "C" bool tuh_xinput_receive_report(
    std::uint8_t dev_addr,
    std::uint8_t instance
) {
    XinputInterface* iface = getInstance(dev_addr, instance);
    if (iface == nullptr || iface->epIn == 0 || iface->epInSize == 0) {
        return false;
    }

    if (!usbh_edpt_claim(dev_addr, iface->epIn)) {
        return false;
    }

    if (!usbh_edpt_xfer(
            dev_addr,
            iface->epIn,
            iface->epInBuffer,
            iface->epInSize
        )) {
        usbh_edpt_release(dev_addr, iface->epIn);
        return false;
    }

    return true;
}

extern "C" bool tuh_xinput_send_report(
    std::uint8_t dev_addr,
    std::uint8_t instance,
    std::uint8_t const* report,
    std::uint16_t len
) {
    XinputInterface* iface = getInstance(dev_addr, instance);
    if (iface == nullptr ||
        iface->epOut == 0 ||
        report == nullptr ||
        len == 0 ||
        len > iface->epOutSize ||
        len > sizeof(iface->epOutBuffer) ||
        !tuh_ready(dev_addr) ||
        usbh_edpt_busy(dev_addr, iface->epOut)) {
        return false;
    }

    if (!usbh_edpt_claim(dev_addr, iface->epOut)) {
        return false;
    }

    std::memcpy(iface->epOutBuffer, report, len);

    if (!usbh_edpt_xfer(
            dev_addr,
            iface->epOut,
            iface->epOutBuffer,
            len
        )) {
        usbh_edpt_release(dev_addr, iface->epOut);
        return false;
    }

    return true;
}

extern "C" bool xinputh_init(void) {
    std::memset(gDevices, 0, sizeof(gDevices));
    return true;
}

extern "C" bool xinputh_xfer_cb(
    std::uint8_t dev_addr,
    std::uint8_t ep_addr,
    xfer_result_t result,
    std::uint32_t xferred_bytes
) {
    const std::uint8_t instanceIndex =
        findInstanceByEndpoint(dev_addr, ep_addr);

    if (instanceIndex == 0xFF) {
        return false;
    }

    XinputInterface* iface = getInstance(dev_addr, instanceIndex);
    if (iface == nullptr) {
        return false;
    }

    if (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) {
        if (result == XFER_RESULT_SUCCESS) {
            tuh_xinput_report_received_cb(
                dev_addr,
                instanceIndex,
                iface->epInBuffer,
                static_cast<std::uint16_t>(xferred_bytes)
            );

            // Rearm only after a successful transfer. Re-queueing an IN
            // transfer after a failed/disconnected transaction can keep stale
            // endpoint state alive while TinyUSB is processing removal.
            if (iface->gameplay &&
                iface->epIn != 0 &&
                iface->epInSize != 0 &&
                tuh_ready(dev_addr)) {
                usbh_edpt_xfer(
                    dev_addr,
                    iface->epIn,
                    iface->epInBuffer,
                    iface->epInSize
                );
            }
        }
    } else if (result == XFER_RESULT_SUCCESS) {
        tuh_xinput_report_sent_cb(
            dev_addr,
            instanceIndex,
            iface->epOutBuffer,
            static_cast<std::uint16_t>(xferred_bytes)
        );
    }

    return true;
}

extern "C" void xinputh_close(std::uint8_t dev_addr) {
    XinputDevice* device = getDevice(dev_addr);
    if (device == nullptr) {
        return;
    }

    for (std::uint8_t i = 0; i < device->instanceCount; ++i) {
        tuh_xinput_umount_cb(dev_addr, i);
    }

    std::memset(device, 0, sizeof(*device));
}

extern "C" bool xinputh_open(
    std::uint8_t rhport,
    std::uint8_t dev_addr,
    tusb_desc_interface_t const* desc_itf,
    std::uint16_t max_len
) {
    (void)rhport;

    if (desc_itf == nullptr) {
        return false;
    }

    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    const bool standardXusb =
        desc_itf->bInterfaceClass == 0xFF &&
        desc_itf->bInterfaceSubClass == 0x5D &&
        desc_itf->bInterfaceProtocol == 0x01;

    const bool exactT29Fallback =
        vid == 0x045E &&
        pid == 0x028E &&
        desc_itf->bNumEndpoints >= 2;

    if (!standardXusb && !exactT29Fallback) {
        return false;
    }

    XinputDevice* device = getDevice(dev_addr);
    if (device == nullptr) {
        return false;
    }

    XinputInterface* iface = nullptr;
    std::uint8_t instanceIndex = 0xFF;

    for (std::uint8_t i = 0; i < CFG_TUH_XINPUT; ++i) {
        XinputInterface* candidate = &device->instances[i];
        if (candidate->epIn == 0 && candidate->epOut == 0) {
            iface = candidate;
            instanceIndex = i;
            break;
        }
    }

    if (iface == nullptr) {
        return false;
    }

    iface->itfNum = desc_itf->bInterfaceNumber;
    iface->type = OAG_XINPUT_XBOX360;
    iface->gameplay = true;

    if (exactT29Fallback) {
        iface->subtype = 0x01;
    }

    const std::uint8_t* cursor =
        reinterpret_cast<const std::uint8_t*>(desc_itf);
    std::uint16_t consumed = desc_itf->bLength;
    cursor = tu_desc_next(cursor);

    std::uint8_t endpointsFound = 0;

    while (consumed < max_len &&
           endpointsFound < desc_itf->bNumEndpoints) {

        const std::uint8_t descriptorLength = tu_desc_len(cursor);
        if (descriptorLength == 0 ||
            consumed + descriptorLength > max_len) {
            break;
        }

        const std::uint8_t descriptorType = tu_desc_type(cursor);
        if (descriptorType == TUSB_DESC_INTERFACE) {
            break;
        }

        if (descriptorType == kReservedDescriptorType &&
            descriptorLength >= 5) {
            iface->subtype = cursor[4];
        }

        if (descriptorType == TUSB_DESC_ENDPOINT) {
            const auto* endpoint =
                reinterpret_cast<const tusb_desc_endpoint_t*>(cursor);

            const std::uint16_t packetSize =
                tu_edpt_packet_size(endpoint);

            if (tu_edpt_dir(endpoint->bEndpointAddress) == TUSB_DIR_IN) {
                if (packetSize == 0 ||
                    packetSize > sizeof(iface->epInBuffer)) {
                    std::memset(iface, 0, sizeof(*iface));
                    return false;
                }

                if (!tuh_edpt_open(dev_addr, endpoint)) {
                    std::memset(iface, 0, sizeof(*iface));
                    return false;
                }

                iface->epIn = endpoint->bEndpointAddress;
                iface->epInSize = packetSize;
            } else {
                if (packetSize == 0 ||
                    packetSize > sizeof(iface->epOutBuffer)) {
                    std::memset(iface, 0, sizeof(*iface));
                    return false;
                }

                if (!tuh_edpt_open(dev_addr, endpoint)) {
                    std::memset(iface, 0, sizeof(*iface));
                    return false;
                }

                iface->epOut = endpoint->bEndpointAddress;
                iface->epOutSize = packetSize;
            }

            ++endpointsFound;
        }

        consumed =
            static_cast<std::uint16_t>(consumed + descriptorLength);
        cursor = tu_desc_next(cursor);
    }

    if (iface->epIn == 0 ||
        iface->epInSize == 0 ||
        iface->epOut == 0 ||
        iface->epOutSize == 0) {
        std::memset(iface, 0, sizeof(*iface));
        return false;
    }

    if (instanceIndex >= device->instanceCount) {
        device->instanceCount =
            static_cast<std::uint8_t>(instanceIndex + 1);
    }

    return true;
}

extern "C" bool xinputh_set_config(
    std::uint8_t dev_addr,
    std::uint8_t itf_num
) {
    const std::uint8_t instanceIndex =
        findInstanceByInterface(dev_addr, itf_num);

    if (instanceIndex == 0xFF) {
        return false;
    }

    completeMount(dev_addr, instanceIndex);
    return true;
}

#endif
