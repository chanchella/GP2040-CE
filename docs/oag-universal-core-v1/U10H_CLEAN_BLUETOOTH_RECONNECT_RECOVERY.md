# U10H — Clean Bluetooth Reconnect Recovery

## Baseline

U10F remains the hardware-good receiver / Player-1 / keyboard-mouse baseline.
U10G added stale-bond recovery, but hardware showed a controller could reach a
solid Bluetooth link without HIDS becoming ready. U10G is therefore not a
hardware-good Bluetooth recovery baseline.

## Root cause addressed

U10G deleted the stale bond and requested fresh SMP pairing inside the same BLE
connection that had already failed re-encryption. On the tested controller the
ACL link remained connected, but the HID service never completed.

U10H changes the recovery state machine:

1. detect missing-key or authentication failure
2. delete only that controller bond
3. deliberately disconnect the failed BLE link
4. keep BLE discovery prioritized for 30 seconds
5. reconnect as a genuinely fresh BLE connection
6. run normal SMP pairing
7. start HIDS only after pairing succeeds
8. consider recovery complete only when HIDS service connected succeeds

A solid controller LED by itself is not treated as success.

## Wired pairing assist

The U10G wired Xbox recovery gesture remains, but now opens a 30-second BLE-only
priority window. If the controller disables Bluetooth advertising while USB is
attached, unplugging the cable or pressing Pair during that window lets the Pico
catch the first HID advertisement immediately.

No generic USB command is sent to force radio pairing and no bulk bond erase is
performed.

## Frozen

U10H does not change:

- 045E:0719 receiver persona
- independent PC controller outputs
- U10F host Player-1 aware primary routing
- keyboard/mouse follows primary
- Start + Share/Back/Guide switching
- rumble and Guide/Home behavior
- USB host / hub / three PIO roots

## Hardware gate

1. Flash U10H.
2. Put the stale Xbox controller in Pairing Mode.
3. Expected: old link is rejected/cleared, controller reconnects fresh, and
   input appears on PC.
4. Then repeat after pairing the controller to another host.
5. Wired assist test: connect controller by USB to Pico, then unplug / press Pair
   if the controller does not advertise while wired. Pico should capture it
   during the BLE-priority window.
