# U10G — Self-Healing Bluetooth Bond Recovery + Wired Pairing Assist

## Baseline

U10F commit:

`7799d270d98ae09e7608f05b15f687a20a459871`

U10F receiver/Player-1/KM behavior is frozen.

## Problem

A BLE controller that was previously bonded to the Pico can be paired to
another host. The controller may then lose or replace the old long-term key,
while the Pico still retains its stale bond. U10F disconnected after failed
re-encryption but did not repair that stale bond.

## Automatic stale-bond repair

On LE re-encryption failure with either:

- ERROR_CODE_PIN_OR_KEY_MISSING
- ERROR_CODE_AUTHENTICATION_FAILURE

U10G follows the BTstack recovery pattern:

1. obtain the identity address from the SM event
2. delete only that device bonding
3. keep the LE link
4. request fresh SMP pairing immediately
5. preserve every other stored Bluetooth peer

No global bond erase is performed.

## Wired pairing assist

Connecting a known Bluetooth-capable Microsoft Xbox controller over USB is an
explicit recovery gesture.

Supported USB identities include Microsoft Xbox One / One S / Elite / Elite 2 /
Adaptive / Series S|X controller families.

When such a wired controller mounts:

1. if an exact stale LE identity is known, delete only that bond
2. otherwise, if there are zero active Bluetooth peers and exactly one stored
   LE bond, delete that single bond
3. if multiple stored bonds exist, delete none
4. restart discovery with BLE first priority

The USB cable does not fabricate a Bluetooth identity and does not bypass the
controller's own radio behavior. If the controller firmware disables Bluetooth
advertising while USB is attached, unplug it or press its Pair button after the
recovery gesture; the Pico is already scanning and will pair immediately when
the controller advertises.

## Frozen from U10F

Unchanged:

- true 045E:0719 Xbox 360 Wireless Receiver persona
- independent PC controllers
- host Player-1 aware primary routing
- keyboard/mouse follows Player 1
- Start + Share/Back/Guide 3-second primary switch
- Home/Guide
- rumble
- USB hub and three PIO USB roots
- four Bluetooth-peer capacity

## Hardware gate

A. Stale-bond automatic recovery:
1. pair controller to Pico
2. pair same controller to another host
3. return controller to Bluetooth Pairing Mode near Pico
4. Pico must recover without flash-nuke or reboot

B. Wired assist:
1. with stale pairing, connect the Xbox controller by USB to Pico
2. leave connected for a few seconds
3. if it does not advertise while wired, unplug cable and enter Pairing Mode
4. Pico must connect on the first fresh advertisement

Until both are physically tested, U10G remains CI-verified only.
