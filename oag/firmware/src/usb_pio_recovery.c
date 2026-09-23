#include "oag/firmware/usb_pio_recovery.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/sync.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "host/hcd.h"

enum {
    OAG_PIO_ROOT_COUNT = 3,
};

static bool root_line_present(root_port_t* root) {
    if (root == NULL || !root->initialized) {
        return false;
    }

    port_pin_status_t const state = pio_usb_bus_get_line_state(root);
    return state == PORT_PIN_FS_IDLE || state == PORT_PIN_LS_IDLE;
}

uint8_t oag_pio_physical_root_mask(void) {
    uint8_t mask = 0;

    for (uint8_t root_index = 0; root_index < OAG_PIO_ROOT_COUNT; ++root_index) {
        root_port_t* root = PIO_USB_ROOT_PORT(root_index);
        if (root_line_present(root)) {
            mask |= (uint8_t)(1u << root_index);
        }
    }

    return mask;
}

void oag_pio_force_reenumerate_connected_roots(void) {
    uint8_t const present_mask = oag_pio_physical_root_mask();

    uint32_t const irq_state = save_and_disable_interrupts();

    for (uint8_t root_index = 0; root_index < OAG_PIO_ROOT_COUNT; ++root_index) {
        root_port_t* root = PIO_USB_ROOT_PORT(root_index);
        if (root == NULL || !root->initialized) {
            continue;
        }

        for (uint8_t ep_index = 0; ep_index < PIO_USB_EP_POOL_CNT; ++ep_index) {
            endpoint_t* ep = PIO_USB_ENDPOINT(ep_index);
            if (ep->size != 0 && ep->root_idx == root_index) {
                memset(ep, 0, sizeof(*ep));
            }
        }

        root->addr0_exists = false;
        root->root_device = NULL;
        root->ep_complete = 0;
        root->ep_error = 0;
        root->ep_stalled = 0;
        root->ints = 0;
        root->event = EVENT_NONE;

        bool const present =
            (present_mask & (uint8_t)(1u << root_index)) != 0;

        root->connected = present;
        root->suspended = present;

        if (present) {
            port_pin_status_t const state = pio_usb_bus_get_line_state(root);
            root->is_fullspeed = state == PORT_PIN_FS_IDLE;
        }
    }

    restore_interrupts(irq_state);

    // Remove stale TinyUSB logical devices first.
    for (uint8_t root_index = 0; root_index < OAG_PIO_ROOT_COUNT; ++root_index) {
        root_port_t* root = PIO_USB_ROOT_PORT(root_index);
        if (root != NULL && root->initialized) {
            hcd_event_device_remove((uint8_t)(root_index + 1u), false);
        }
    }

    // Then enumerate only physically occupied roots.
    for (uint8_t root_index = 0; root_index < OAG_PIO_ROOT_COUNT; ++root_index) {
        if ((present_mask & (uint8_t)(1u << root_index)) != 0) {
            hcd_event_device_attach((uint8_t)(root_index + 1u), false);
        }
    }
}
