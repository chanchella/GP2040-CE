# OAG Vortex — Third-Party Reference Policy

This document records which upstream projects may be used directly and
which must remain reference-only when developing OAG Vortex.

## GP2040-CE

- Upstream: OpenStickCommunity/GP2040-CE
- License: MIT
- Role: Primary firmware/platform-output base.
- Direct reuse/modification: Allowed under MIT terms.
- Authentication implementations must remain within legitimate supported
  authentication/passthrough flows. Do not add extracted, cloned, fabricated,
  or otherwise unauthorized platform credentials.

## Pico-PIO-USB

- Upstream: sekigon-gonnoc/Pico-PIO-USB and the GP2040-CE pinned fork/submodule.
- License: MIT.
- Role: PIO USB Host transport.
- Direct reuse/modification: Allowed under MIT terms.
- OAG currently uses one TinyUSB/PIO host controller with multiple configured
  root ports; do not assume one independent PIO state-machine/interrupt set per
  physical connector unless verified for the pinned implementation.

## HID-Remapper

- Upstream: jfedor2/hid-remapper
- License: MIT unless a file states otherwise.
- Role: Reference for usage-based remapping architecture, configurable mapping,
  relative/absolute HID handling, layers, and WebHID-style configuration ideas.
- Direct reuse: Permitted only after checking the license header of the exact
  file being reused and preserving required MIT notices.
- OAG strategy: Prefer a small OAG-native mapping engine tailored to the
  UniversalInput/UniversalOutput slot architecture rather than importing the
  entire remapper runtime.

## BlueRetro

- Upstream: darthcloud/BlueRetro
- License: Apache-2.0.
- Role: Reference for HID descriptor parsing, controller protocol behavior,
  PlayStation/Xbox/Nintendo report interpretation, and regression/test vectors.
- Direct reuse: Possible under Apache-2.0 with required copyright/license
  notices and any applicable NOTICE obligations.
- OAG strategy: Prefer OAG-native parser adapters using BlueRetro as a
  behavioral/reference source. If code is copied or adapted directly, keep the
  Apache-2.0 provenance in that file.

## GIMX

- Upstream: matlo/GIMX
- License: GPL-3.0.
- Role: Behavioral reference for mouse-to-analog concepts such as sensitivity,
  acceleration/ballistic response, deadzone compensation, and console-oriented
  mouse translation.
- Direct code reuse in the OAG firmware: Prohibited by project policy unless the
  distribution/licensing strategy is intentionally changed to comply with GPL.
- OAG strategy: Clean-room implementation of MouseAimEngine based on documented
  behavior, mathematical requirements, and independently derived tests. Do not
  copy GIMX implementation code.

## Architectural rule

Third-party protocol knowledge must enter OAG through explicit layers:

Transport -> Device Registry -> Protocol Parser -> Normalized Input Slot
-> Mapping/Translation -> Logical Output Slot -> Platform Output/Profile

Do not allow device-brand-specific conditions to leak into unrelated output
drivers. Prefer protocol/signature classification and small quirk records.
