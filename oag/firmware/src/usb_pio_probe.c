#include "oag/firmware/usb_pio_probe.h"

#include <stdbool.h>
#include <stdint.h>

#include "pio_usb.h"
#include "pio_usb_ll.h"

enum {
    OAG_PIO_ROOT_COUNT = 3,
};

static bool root_line_present(root_port_t* root) {
    if (root == NULL || !root->initialized) {
        return false;
    }

    port_pin_status_t const state = pio_usb_bus_get_line_state(root);

    return state == PORT_PIN_FS_IDLE ||
           state == PORT_PIN_LS_IDLE;
}

uint8_t oag_pio_physical_root_mask(void) {
    uint8_t mask = 0;

    for (uint8_t root_index = 0;
         root_index < OAG_PIO_ROOT_COUNT;
         ++root_index) {
        if (root_line_present(PIO_USB_ROOT_PORT(root_index))) {
            mask |= (uint8_t)(1u << root_index);
        }
    }

    return mask;
}
