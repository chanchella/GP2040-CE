# U5B — Platform Selection and Activation Gate

U5B adds the selection policy needed for a future single multi-platform UF2
without changing the current live USB identity.

## Selection order

The selector resolves a requested platform in this order:

1. boot-time override;
2. fixed requested profile when selection mode is Fixed;
3. verified host fingerprint when Auto mode has one;
4. persisted preference;
5. safe default fallback: PC_XINPUT_360.

The selection object is pure policy. It does not switch USB descriptors by
itself.

## Why selection must happen before enumeration

A target platform interprets the USB descriptors presented during device
enumeration. Therefore the active PlatformOutputDriver must be selected before
the target-facing TinyUSB device stack exposes the final profile.

Future boot flow can therefore become:

USB Host input/bootstrap
-> boot override / stored profile / verified host evidence
-> PlatformSelector
-> PlatformActivationGate
-> select target driver/descriptors
-> target-facing USB device enumeration

The hardware-verified PC profile is not changed by U5B.

## Activation gate

A selected profile cannot activate merely because it exists in the registry.

The gate requires:

- implementation status == RuntimeAvailable;
- if authentication is required, a compatible IPlatformAuthProvider;
- the auth provider must be ready for the session.

This prevents Switch/PS/Xbox foundations from being advertised as complete
before their runtime/session/auth gates are closed.

## Auto-detection limitation

U5B intentionally treats HostFingerprint as evidence supplied by a future,
separately verified detector. It does not claim universal zero-touch console
detection.

USB hosts do not provide one universal platform identifier before a device
must choose its descriptors. Reliable detection may require platform-specific
enumeration fingerprints and controlled re-enumeration, and some closed
platforms may still require an explicit persisted/boot-selected profile.

The architecture supports both automatic evidence and explicit fallback
selection without coupling either mechanism to input parsing.
