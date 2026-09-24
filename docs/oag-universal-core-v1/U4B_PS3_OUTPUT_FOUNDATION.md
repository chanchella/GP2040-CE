# U4B — PlayStation 3 Output Foundation

U4B adds a clean PlayStation 3 / DualShock 3 semantic report encoder and
reverse feedback decoder while keeping the live firmware default on the
hardware-verified PC Xbox 360/XInput profile.

## Golden reference

Reference baseline:

- golden/oag-g2e3-home-km-led
- commit fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b
- headers/drivers/ps3/PS3Descriptors.h
- src/drivers/ps3/PS3Driver.cpp

The Golden branch is read-only.

## Forward report foundation

LogicalGamepadState is mapped to the Golden DS3 semantic layout:

- Select / Start
- L3 / R3
- PS
- D-pad digital bits
- Cross / Circle / Square / Triangle
- L1 / R1
- digital L2 / R2 presence
- four 8-bit sticks
- analog D-pad pressures
- analog trigger pressures
- analog face/shoulder pressures
- plugged/full-battery/wired-rumble status
- neutral Sixaxis sensor centers

## Reverse feedback foundation

The PS3 output payload decoder extracts:

- left/large rumble motor
- right/small rumble motor
- player LED mask

into normalized OAG feedback data.

## Important descriptor gate

The Golden source contains a stale comment saying the packed PS3Report is
"49 length". Counting the actual packed semantic fields gives 51 bytes.

The HID report descriptor itself describes a different aggregate input length.
Therefore U4B deliberately does **not** promote PS3 to RuntimeAvailable.

Before live PS3 USB output is enabled, U4C must reconcile:

1. exact bytes submitted by the Golden runtime;
2. report-ID handling;
3. the report descriptor's declared payload length;
4. feature reports F2/F5/F7/F8 and EF state;
5. physical PS3 enumeration.

Until that gate closes, PlayStation 3 is SOFTWARE FOUNDATION only.
