/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 OpenStickCommunity (gp2040-ce.info)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb_option.h"

#if (CFG_TUH_ENABLED && CFG_TUH_XINPUT)

#include "hardware/structs/usb.h"

#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include "drivers/shared/xinput_host.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

typedef struct
{
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t type;
    uint8_t subtype;
    uint8_t protocol;
    bool gameplay;

    uint16_t epin_size;
    uint16_t epout_size;

    uint8_t epin_buf[CFG_TUH_XINPUT_EPIN_BUFSIZE];
    uint8_t epout_buf[CFG_TUH_XINPUT_EPOUT_BUFSIZE];
} xinputh_interface_t;

typedef struct
{
    uint8_t inst_count;
    xinputh_interface_t instances[CFG_TUH_XINPUT];
} xinputh_device_t;
static xinputh_device_t _xinputh_dev[CFG_TUH_DEVICE_MAX];

#define XINPUT_DESC_TYPE_RESERVED 0x21

typedef struct {
    uint8_t bLength; // Length of this descriptor.
    uint8_t bDescriptorType; // CONFIGURATION descriptor type (USB_DESCRIPTOR_CONFIGURATION).
    uint8_t flags;
    uint8_t reserved;
    uint8_t subtype;
    uint8_t reserved2;
    uint8_t bEndpointAddressIn;
    uint8_t bMaxDataSizeIn;
    uint8_t reserved3[5];
    uint8_t bEndpointAddressOut;
    uint8_t bMaxDataSizeOut;
    uint8_t reserved4[2];
} __attribute__((packed)) XBOX_ID_DESCRIPTOR;

//------------- Internal prototypes -------------//

// Get HID device & interface
TU_ATTR_ALWAYS_INLINE static inline xinputh_device_t *get_dev(uint8_t dev_addr);
TU_ATTR_ALWAYS_INLINE static inline xinputh_interface_t *get_instance(uint8_t dev_addr, uint8_t instance);
static uint8_t get_instance_id_by_itfnum(uint8_t dev_addr, uint8_t itf);
static uint8_t get_instance_id_by_epaddr(uint8_t dev_addr, uint8_t ep_addr);

//--------------------------------------------------------------------+
// Interface API
//--------------------------------------------------------------------+

uint8_t tuh_xinput_instance_count(uint8_t dev_addr) {
    return get_dev(dev_addr)->inst_count;
}

bool tuh_xinput_mounted(uint8_t dev_addr, uint8_t instance) {
    if (instance >= get_dev(dev_addr)->inst_count) return false;
    xinputh_interface_t *hid_itf = get_instance(dev_addr, instance);
    return (hid_itf->ep_in != 0) || (hid_itf->ep_out != 0);
}

//--------------------------------------------------------------------+
// Interrupt Endpoint API
//--------------------------------------------------------------------+

bool tuh_xinput_receive_report(uint8_t dev_addr, uint8_t instance) {
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);

    // claim endpoint
    TU_VERIFY(usbh_edpt_claim(dev_addr, xid_itf->ep_in));

    if (!usbh_edpt_xfer(dev_addr, xid_itf->ep_in, xid_itf->epin_buf, xid_itf->epin_size)) {
        usbh_edpt_release(dev_addr, xid_itf->ep_in);
        return false;
    }

    return true;
}

bool tuh_xinput_receive_vendor_report(uint8_t dev_addr, uint8_t instance, uint8_t request, uint16_t value, uint8_t index, uint16_t length, uint8_t * recvBuf) {
    const tusb_control_request_t xfer_ctrl_req = {
            .bmRequestType_bit {
                .recipient = TUSB_REQ_RCPT_INTERFACE,
                .type = TUSB_REQ_TYPE_VENDOR,
                .direction = TUSB_DIR_IN,
            },
            .bRequest = request,
            .wValue = value,
            .wIndex = TU_U16(index, 0x03),
            .wLength = length
    };
    tuh_xfer_t xfer = {
        .daddr       = dev_addr,
        .ep_addr     = 0,
        .setup       = &xfer_ctrl_req,
        .buffer      = recvBuf,
        .complete_cb = NULL,
    };
    return tuh_control_xfer(&xfer);
}


bool tuh_xinput_send_report(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);

    bool ret = false;

    // claim endpoint
    TU_ASSERT(len <= xid_itf->epout_size);
    bool tuh_rdy = tuh_ready(dev_addr);
    bool edpt_busy = usbh_edpt_busy(dev_addr, xid_itf->ep_out);
    if (tuh_rdy &&
        (xid_itf->ep_out != 0) && (!edpt_busy)) {
        TU_VERIFY(usbh_edpt_claim(dev_addr, xid_itf->ep_out));
        memcpy(xid_itf->epout_buf, report, len);
        if (!usbh_edpt_xfer(dev_addr, xid_itf->ep_out, xid_itf->epout_buf, len)) {
            usbh_edpt_release(dev_addr, xid_itf->ep_out);
            ret = false;
        }
        ret = true;

    } 

    return ret;
}

bool tuh_xinput_send_vendor_report(uint8_t dev_addr, uint8_t instance, uint8_t request, uint16_t value, uint8_t index, uint16_t length, uint8_t * sendBuf) {
    const tusb_control_request_t xfer_ctrl_req = {
            .bmRequestType_bit {
                .recipient = TUSB_REQ_RCPT_INTERFACE,
                .type = TUSB_REQ_TYPE_VENDOR,
                .direction = TUSB_DIR_OUT,
            },
            .bRequest = request,
            .wValue = value,
            .wIndex = TU_U16(index, 0x03),
            .wLength = length
    };
    tuh_xfer_t xfer = {
        .daddr       = dev_addr,
        .ep_addr     = 0,
        .setup       = &xfer_ctrl_req,
        .buffer      = sendBuf,
        .complete_cb = NULL,
    };
    return tuh_control_xfer(&xfer);
}

bool tuh_xinput_ready(uint8_t dev_addr, uint8_t instance) {
    TU_VERIFY(tuh_xinput_mounted(dev_addr, instance));

    xinputh_interface_t *hid_itf = get_instance(dev_addr, instance);
    return !usbh_edpt_busy(dev_addr, hid_itf->ep_in);
}

//--------------------------------------------------------------------+
// USBH API
//--------------------------------------------------------------------+
bool xinputh_init(void) {
    tu_memclr(_xinputh_dev, sizeof(_xinputh_dev));
    return true;
}

bool xinputh_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    uint8_t const dir = tu_edpt_dir(ep_addr);
    uint8_t const instance = get_instance_id_by_epaddr(dev_addr, ep_addr);

    if (instance == 0xff) {
        return false;
    }

    xinputh_interface_t *xinput_itf = get_instance(dev_addr, instance);

    if (dir == TUSB_DIR_IN) {
        if (result == XFER_RESULT_SUCCESS) {
            tuh_xinput_report_received_cb(
                dev_addr,
                instance,
                xinput_itf->epin_buf,
                (uint16_t)xferred_bytes
            );
        }

        // Keep the gameplay endpoint armed continuously.
        if (xinput_itf->gameplay && xinput_itf->ep_in != 0 && xinput_itf->epin_size != 0) {
            usbh_edpt_xfer(
                dev_addr,
                xinput_itf->ep_in,
                xinput_itf->epin_buf,
                xinput_itf->epin_size
            );
        }
    } else {
        if (result == XFER_RESULT_SUCCESS && tuh_xinput_report_sent_cb) {
            tuh_xinput_report_sent_cb(
                dev_addr,
                instance,
                xinput_itf->epout_buf,
                xferred_bytes
            );
        }
    }

    return true;
}

void xinputh_close(uint8_t dev_addr) {
    TU_VERIFY(dev_addr <= CFG_TUH_DEVICE_MAX, );
    xinputh_device_t *hid_dev = get_dev(dev_addr);
    if (tuh_xinput_umount_cb) {
        for (uint8_t inst = 0; inst < hid_dev->inst_count; inst++) tuh_xinput_umount_cb(dev_addr, inst);
    }

    tu_memclr(hid_dev, sizeof(xinputh_device_t));
}

bool xinputh_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *desc_itf, uint16_t max_len) {
    (void)rhport;

    TU_VERIFY(desc_itf != nullptr, false);

    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    const bool xbox360Signature =
        desc_itf->bInterfaceSubClass == 0x5D &&
        (
            desc_itf->bInterfaceProtocol == 0x01 ||
            desc_itf->bInterfaceProtocol == 0x02 ||
            desc_itf->bInterfaceProtocol == 0x03 ||
            desc_itf->bInterfaceProtocol == 0x81
        );

    const bool xboxOneSignature =
        desc_itf->bInterfaceSubClass == 0x47 &&
        desc_itf->bInterfaceProtocol == 0xD0 &&
        desc_itf->bNumEndpoints > 0;

    // Exact compatibility fallback for the wired T29/XUSB identity
    // already proven in the previous firmware.
    const bool exactT29Fallback =
        vid == 0x045E &&
        pid == 0x028E &&
        desc_itf->bNumEndpoints >= 2;

    TU_VERIFY(
        xbox360Signature ||
        xboxOneSignature ||
        exactT29Fallback,
        false
    );

    xinputh_interface_t *p_xinput = nullptr;
    for (uint8_t i = 0; i < CFG_TUH_XINPUT; i++) {
        xinputh_interface_t *candidate = get_instance(dev_addr, i);
        if (candidate->ep_in == 0 && candidate->ep_out == 0) {
            p_xinput = candidate;
            break;
        }
    }
    TU_VERIFY(p_xinput != nullptr, false);

    p_xinput->itf_num = desc_itf->bInterfaceNumber;
    p_xinput->protocol = desc_itf->bInterfaceProtocol;

    if (xboxOneSignature) {
        p_xinput->type = XBOXONE;
        p_xinput->gameplay = true;
    } else {
        p_xinput->type = XBOX360;
        p_xinput->gameplay =
            desc_itf->bInterfaceProtocol == 0x01 ||
            exactT29Fallback;

        // If a T29 clone omits the Xbox ID subtype descriptor,
        // classify the exact known identity as a normal gamepad.
        if (exactT29Fallback) {
            p_xinput->subtype = 0x01;
        }
    }

    uint8_t const *cursor = reinterpret_cast<uint8_t const *>(desc_itf);
    uint16_t consumed = desc_itf->bLength;
    cursor = tu_desc_next(cursor);
    uint8_t endpointsFound = 0;

    while (consumed < max_len && endpointsFound < desc_itf->bNumEndpoints) {
        const uint8_t descriptorLength = tu_desc_len(cursor);

        if (descriptorLength == 0 || consumed + descriptorLength > max_len) {
            break;
        }

        const uint8_t descriptorType = tu_desc_type(cursor);

        // Never consume endpoints belonging to the next interface.
        if (descriptorType == TUSB_DESC_INTERFACE) {
            break;
        }

        // Xbox 360 ID descriptor. It is useful when present, but unlike
        // upstream G1A we do not require it to exist or to be adjacent.
        if (
            p_xinput->type == XBOX360 &&
            descriptorType == XINPUT_DESC_TYPE_RESERVED &&
            descriptorLength >= 5
        ) {
            p_xinput->subtype = cursor[4];
        }

        if (descriptorType == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const *desc_ep =
                reinterpret_cast<tusb_desc_endpoint_t const *>(cursor);

            if (!tuh_edpt_open(dev_addr, desc_ep)) {
                tu_memclr(p_xinput, sizeof(xinputh_interface_t));
                return false;
            }

            const uint16_t packetSize = tu_edpt_packet_size(desc_ep);

            if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
                p_xinput->ep_in = desc_ep->bEndpointAddress;
                p_xinput->epin_size = packetSize;
            } else {
                p_xinput->ep_out = desc_ep->bEndpointAddress;
                p_xinput->epout_size = packetSize;
            }

            endpointsFound++;
        }

        consumed += descriptorLength;
        cursor = tu_desc_next(cursor);
    }

    if (p_xinput->ep_in == 0 || p_xinput->epin_size == 0) {
        tu_memclr(p_xinput, sizeof(xinputh_interface_t));
        return false;
    }

    // Wired T29/XUSB exposes both interrupt IN and OUT endpoints.
    if (p_xinput->gameplay && p_xinput->type == XBOX360 && p_xinput->ep_out == 0) {
        tu_memclr(p_xinput, sizeof(xinputh_interface_t));
        return false;
    }

    get_dev(dev_addr)->inst_count++;
    return true;
}

//--------------------------------------------------------------------+
// Set Configure
//--------------------------------------------------------------------+
static void config_driver_mount_complete(uint8_t dev_addr, uint8_t instance);
static void process_set_config(tuh_xfer_t *xfer);

bool xinputh_set_config(uint8_t dev_addr, uint8_t itf_num) {
    uint8_t instance = get_instance_id_by_itfnum(dev_addr, itf_num);
    if (instance == 0xff) {
        return false;
    }

    config_driver_mount_complete(dev_addr, instance);
    return true;
}

static void config_driver_mount_complete(uint8_t dev_addr, uint8_t instance) {
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);

    // Enumeration is complete from the listener's perspective.
    tuh_xinput_mount_cb(dev_addr, instance, xid_itf->type, xid_itf->subtype);

    // Some inexpensive wired XUSB clones remain silent until normal
    // OUT traffic is seen. This is the same harmless Player-1 LED
    // packet used by the T29 host path that previously worked.
    if (
        xid_itf->type == XBOX360 &&
        xid_itf->gameplay &&
        xid_itf->ep_out != 0 &&
        xid_itf->epout_size >= 3 &&
        !usbh_edpt_busy(dev_addr, xid_itf->ep_out) &&
        usbh_edpt_claim(dev_addr, xid_itf->ep_out)
    ) {
        static const uint8_t player1Led[3] = {0x01, 0x03, 0x06};
        memcpy(xid_itf->epout_buf, player1Led, sizeof(player1Led));

        if (!usbh_edpt_xfer(
                dev_addr,
                xid_itf->ep_out,
                xid_itf->epout_buf,
                sizeof(player1Led)
            )) {
            usbh_edpt_release(dev_addr, xid_itf->ep_out);
        }
    }

    // Arm only actual gameplay interfaces. Auxiliary Xbox 360
    // interfaces are still claimed but are not fed into the pad slot.
    if (xid_itf->gameplay) {
        tuh_xinput_receive_report(dev_addr, instance);
    }

    // Notify TinyUSB that driver configuration is complete.
    usbh_driver_set_config_complete(dev_addr, xid_itf->itf_num);
}

//--------------------------------------------------------------------+
// Helper
//--------------------------------------------------------------------+

// Get Device by address
TU_ATTR_ALWAYS_INLINE static inline xinputh_device_t *get_dev(uint8_t dev_addr) {
    return &_xinputh_dev[dev_addr - 1];
}

// Get Interface by instance number
TU_ATTR_ALWAYS_INLINE static inline xinputh_interface_t *get_instance(uint8_t dev_addr, uint8_t instance) {
    return &_xinputh_dev[dev_addr - 1].instances[instance];
}

// Get instance ID by interface number
static uint8_t get_instance_id_by_itfnum(uint8_t dev_addr, uint8_t itf) {
    for (uint8_t inst = 0; inst < CFG_TUH_XINPUT; inst++) {
        xinputh_interface_t *hid = get_instance(dev_addr, inst);
        if ((hid->itf_num == itf)) return inst;
    }
    return 0xff;
}

// Get instance ID by endpoint address
static uint8_t get_instance_id_by_epaddr(uint8_t dev_addr, uint8_t ep_addr) {
    for (uint8_t inst = 0; inst < CFG_TUH_XINPUT; inst++) {
        xinputh_interface_t *hid = get_instance(dev_addr, inst);
        if ((ep_addr == hid->ep_in) || (ep_addr == hid->ep_out)) return inst;
    }
    return 0xff;
}

#endif
