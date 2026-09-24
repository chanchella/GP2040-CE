# U4D — PlayStation 4 Gameplay / Feedback Foundation

U4D adds a clean PS4 gameplay report encoder and reverse feedback decoder,
derived from the read-only Golden PS4 implementation.

## Golden reference

- branch: golden/oag-g2e3-home-km-led
- commit: fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b
- headers/drivers/ps4/PS4Descriptors.h
- src/drivers/ps4/PS4Driver.cpp

## Forward gameplay report

LogicalGamepadState maps to the 64-byte PS4 input report:

- report ID 0x01
- four 8-bit sticks
- 4-bit D-pad hat
- Square / Cross / Circle / Triangle
- L1 / R1 / digital L2 / R2
- Share / Options
- L3 / R3
- PS
- 6-bit report counter
- analog L2 / R2

Vendor-specific sensor and touch bytes remain neutral in U4D. Motion/touch are
separate capability gates.

## Reverse feedback

PS4 Output Report ID 0x05 is decoded into:

- left/large rumble motor
- right/small rumble motor
- RGB lightbar
- LED blink on/off

## Authentication boundary

PS4 remains dependent on:

AuthRequirementKind::LiveOfficialDonorPassthrough
AuthDonorFamily::DualShock4

The Golden runtime handles PS4 authentication through nonce pages, CRC32 and
signature-response feature reports. U4D does not fabricate signing material.

PS4 moves from Planned to SoftwareFoundation, not RuntimeAvailable.

A later gate must wire the feature-report/auth passthrough state machine and
then perform physical PS4 enumeration/testing.

## PS5

No DualSense/PS5 runtime driver exists in the Golden baseline used here.
PS5 therefore remains Planned until a separately verified protocol reference is
introduced; U4D does not infer PS5 behavior from PS4.
