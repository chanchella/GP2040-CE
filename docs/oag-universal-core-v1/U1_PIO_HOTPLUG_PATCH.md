# U1 PIO USB hot-plug patch

Pinned dependency:

- Pico-PIO-USB: `37965f8895fffb4ffacace86e1f731f908dc18e0`

U1 applies two deterministic build-time changes and verifies their exact
preconditions before modifying the checked-out dependency:

1. Root port count: 2 -> 3.
2. Device close: reset each matching `endpoint_t` completely with
   `memset(ep, 0, sizeof(*ep))` instead of clearing only `size` and
   `has_transfer`.

Reason for patch 2:

- Physical test showed that XUSB input works after boot and that axis
  orientation can be corrected in the protocol layer.
- Moving the same controller between P1/P2/P3 still required a Pico
  power-cycle.
- A power-cycle clears the PIO USB endpoint pool completely.
- The pinned dependency's close path leaves multiple endpoint lifecycle
  fields untouched, allowing stale state to survive hot-unplug and reuse.

This patch is deliberately below the OAG DeviceRegistry/slot layer.
It does not add Last-Active arbitration and does not change platform
output behavior.

Hardware verification remains pending until the user tests the exact
replacement UF2 on P1 -> P2 -> P3 without reconnecting Pico power.
