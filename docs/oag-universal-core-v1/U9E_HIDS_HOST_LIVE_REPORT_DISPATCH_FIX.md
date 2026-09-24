# U9E — HIDS Host Live Report Dispatch Fix

Hardware on U9D proved that the Bluetooth controller pairs and remains
connected (solid controller LED) while no input reaches Windows.

The exact cause is in the SDK 2.3 / BTstack 075a078 HIDS Host callback
contract:

- HIDS service setup events may be delivered with packet_type
  HCI_EVENT_PACKET.
- Live HID notification reports are delivered with packet_type
  HCI_EVENT_GATTSERVICE_META.
- The packet body itself still starts with HCI_EVENT_GATTSERVICE_META.

U9D's handleLeHidPacket rejected every callback whose packet_type was not
HCI_EVENT_PACKET. Therefore pairing/HIDS could succeed and the controller
could remain connected while all live input reports were dropped before the
parser.

U9E mirrors the historical BluetoothHIDMaster behavior: packet_type is not
used as the gate. The packet body's event type is validated instead.

No pairing, discovery, SDK, BTstack, CYW43, USB host, G2E3 init-order, parser,
slot-routing or XInput descriptor changes are made in this phase.
