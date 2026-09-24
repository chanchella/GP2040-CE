# U8J — BLE HIDS Encryption Gate

Hardware diagnostics from U8I identified a BLE Stage-5 failure with status
0x0F. In ATT, 0x0F is ATT_ERROR_INSUFFICIENT_ENCRYPTION.

U8J fixes the ordering issue:

1. Pairing/re-encryption success no longer starts HIDS immediately.
2. OAG checks gap_security_level(handle).
3. HIDS starts only at LEVEL_2 or above.
4. GAP_EVENT_SECURITY_LEVEL drives deferred HIDS startup.
5. If HIDS still returns ATT 0x0F, OAG clears only the failed HIDS client
   state, keeps the BLE ACL link open, requests LEVEL_2 again, and retries
   HIDS after security readiness.

USB descriptors, wired host behavior, XInput topology, and Bluetooth
Peripheral mode remain unchanged.
