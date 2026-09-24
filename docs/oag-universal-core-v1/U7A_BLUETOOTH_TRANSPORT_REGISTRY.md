# U7A — Bluetooth Transport Registry Foundation

U7A makes Bluetooth a first-class transport in the OAG Device Registry.

Bluetooth devices use a generation-safe handle containing the transport kind
(Classic or LE), connection handle and HID service instance.

They share the same DeviceId pool and LogicalSlotManager capacity as USB
devices. Disconnect/reconnect advances generation exactly like USB so stale
Bluetooth callbacks cannot target a new device.

This gate is software-only transport plumbing. U6C remains the rollback
firmware baseline until the live BTstack gate passes.
