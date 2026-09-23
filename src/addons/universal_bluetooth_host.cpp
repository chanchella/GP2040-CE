#include "addons/universal_bluetooth_host.h"

#if OAG_BLUETOOTH_HOST_ENABLED && defined(PICO_CYW43_SUPPORTED)

#include "btstack.h"
#include "btstack_run_loop_embedded.h"
#include "pico/cyw43_arch.h"

#include "device/oag_identity.h"

namespace {

static btstack_packet_callback_registration_t hciEventCallback {};

static uint8_t classicDescriptorStorage[1024] {};
static uint8_t leDescriptorStorage[1024] {};

static void btPacketHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
    uint16_t size
) {
    (void)channel;
    (void)size;

    if (packetType != HCI_EVENT_PACKET || packet == nullptr) {
        return;
    }

    const uint8_t event = hci_event_packet_get_type(packet);

    if (event == BTSTACK_EVENT_STATE) {
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
            return;
        }

        // Coexistence gate: keep the Bluetooth controller powered but idle.
        // Discovery is intentionally started only by an explicit Pair action
        // in the next phase. Continuous Classic + BLE scanning here adds
        // avoidable radio/event-loop load while USB device reports are active.
        return;
    }

    if (event == HCI_EVENT_HID_META) {
        const uint8_t subevent =
            hci_event_hid_meta_get_subevent_code(packet);

        if (
            subevent == HID_SUBEVENT_INCOMING_CONNECTION &&
            hid_subevent_incoming_connection_get_status(packet) ==
                ERROR_CODE_SUCCESS
        ) {
            hid_host_accept_connection(
                hid_subevent_incoming_connection_get_hid_cid(packet),
                HID_PROTOCOL_MODE_REPORT
            );
        }
    }
}

} // namespace

#endif

bool UniversalBluetoothHostAddon::available() {
#if OAG_BLUETOOTH_HOST_ENABLED && defined(PICO_CYW43_SUPPORTED)
    return true;
#else
    return false;
#endif
}

void UniversalBluetoothHostAddon::setup() {
#if OAG_BLUETOOTH_HOST_ENABLED && defined(PICO_CYW43_SUPPORTED)
    if (cyw43_arch_init() != PICO_OK) {
        initialized = false;
        return;
    }

    l2cap_init();

    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_YES_NO);
    sm_set_authentication_requirements(
        SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING
    );

    gatt_client_init();

    hid_host_init(
        classicDescriptorStorage,
        sizeof(classicDescriptorStorage)
    );
    hid_host_register_packet_handler(btPacketHandler);

    hids_host_init(
        leDescriptorStorage,
        sizeof(leDescriptorStorage)
    );

    gap_set_local_name(OAG_BLUETOOTH_DEVICE_NAME);
    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_SNIFF_MODE |
        LM_LINK_POLICY_ENABLE_ROLE_SWITCH
    );
    hci_set_master_slave_policy(HCI_ROLE_MASTER);

    hciEventCallback.callback = &btPacketHandler;
    hci_add_event_handler(&hciEventCallback);

    // Host-only foundation: do not advertise or become discoverable.
    gap_connectable_control(0);
    gap_discoverable_control(0);

    hci_power_control(HCI_POWER_ON);
    initialized = true;
#endif
}

void UniversalBluetoothHostAddon::preprocess() {
#if OAG_BLUETOOTH_HOST_ENABLED && defined(PICO_CYW43_SUPPORTED)
    if (!initialized) {
        return;
    }

    btstack_run_loop_embedded_execute_once();
#endif
}
