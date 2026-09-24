# U4C — Xbox One / Series XGIP Gameplay Foundation

U4C adds the gameplay-side XGIP foundation for Xbox One and Xbox Series while
keeping the live firmware default on the hardware-verified PC Xbox 360/XInput
profile.

## Golden reference

Read-only source reference:

- golden/oag-g2e3-home-km-led
- commit fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b
- headers/drivers/shared/xgip_protocol.h
- headers/drivers/xbone/XBOneDescriptors.h
- src/drivers/xbone/XBOneDriver.cpp

## XGIP header model

The Core now understands the 4-byte XGIP header:

- command
- 4-bit client
- needs-ACK
- internal
- chunk-start
- chunked
- sequence
- payload length

The parser also recognizes the extra two chunk-position bytes used by chunked
packets. Full chunk assembly remains a later session-layer gate.

## Gameplay packet encoder

LogicalGamepadState can now produce the Golden-compatible 36-byte
GIP_INPUT_REPORT:

- A/B/X/Y
- Start / Back
- D-pad
- LB / RB
- L3 / R3
- 10-bit LT / RT
- signed 16-bit sticks
- OAG canonical Y axis inverted only at the XGIP wire boundary
- 18 reserved trailing bytes

Guide is intentionally not embedded in gameplay reports. The Golden runtime
uses GIP_VIRTUAL_KEYCODE for Guide, and U4C follows that model.

## Additional packet foundations

U4C also encodes:

- Guide pressed/released virtual-key packets: payload {01|00, 5B}
- internal XGIP keepalive packet: {80,00,00,00}

## Authentication boundary

Xbox One and Xbox Series profiles remain:

AuthRequirementKind::LiveOfficialDonorPassthrough

U4C does not fabricate authentication data and does not claim console
enumeration. The gameplay protocol is now SoftwareFoundation; the next gate is
the XGIP session state machine:

announce -> descriptor chunks / ACK -> official donor auth passthrough ->
authenticated gameplay.

Therefore Xbox One and Xbox Series move from Planned to SoftwareFoundation,
not RuntimeAvailable.
