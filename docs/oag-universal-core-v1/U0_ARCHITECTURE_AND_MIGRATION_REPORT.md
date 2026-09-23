# OAG UNIVERSAL CORE V1 — ARCHITECTURE & MIGRATION REPORT

Status: PHASE U0 ARCHITECTURE GATE  
Project: OAG Universal Core V1  
Repository: chanchella/GP2040-CE  
Development branch: chatgpt/oag-universal-core-v1  
Historical recovery baseline: golden/oag-g2e3-home-km-led  
Golden commit: fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b  
Target board: Raspberry Pi Pico 2 W / RP2350

## 1. Executive decision

G2E3 contains several hardware-proven behaviors and useful protocol implementations, but its current shape is not suitable as the long-term architecture of OAG Universal Core V1.

The V1 design will therefore use G2E3 as a reference implementation and extraction source, not as the runtime skeleton.

The new firmware will be a Pico 2 W focused target with explicit separation between:

Physical transport -> device discovery -> protocol driver -> universal physical state -> logical binding/slot layer -> mapping pipeline -> universal logical output -> platform output driver -> optional authentication provider -> target platform.

The reverse path is:

Target platform feedback -> platform driver -> universal feedback -> logical binding -> physical device protocol driver -> transport.

No U1 firmware code is started by U0.

## 2. Current Golden facts

Verified against GitHub:

- golden/oag-g2e3-home-km-led resolves to fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b.
- chatgpt/oag-universal-core-v1 was identical to the Golden commit at U0 start.
- integration/oag-universal-core-v1 was also at the Golden commit at U0 start.
- antigravity/g2e3-work was at the Golden commit when U0 began, but it remains read-only and outside this project.
- The Golden tree SHA is 0470b753d75b8735d2fef6221baa3eea655c907e.
- The Golden commit message is G2E3: harden K/M host, keyboard LEDs, and semantic Guide.
- The Golden repository tree contains 1883 entries.
- The Golden build uses Raspberry Pi Pico SDK 2.1.1 or later and C++17.

Golden submodules verified:

- lib/pico_pio_usb -> OpenStickCommunity/Pico-PIO-USB commit 37965f8895fffb4ffacace86e1f731f908dc18e0.
- lib/tinyusb -> OpenStickCommunity/tinyusb commit 9865cba11ecbcdd25237ba9cf4ccbe3fd1fd821d.

GitHub Actions evidence for the exact Golden SHA:

- OAG Vortex G2C2 BTstack Pico2W completed successfully on the exact Golden SHA.
- Successful run 35909496434 emitted UF2 and ELF artifacts.
- A second successful run exists for the same SHA.
- This supports CI VERIFIED for the Golden build pipeline at that SHA.
- It does not by itself create any new HARDWARE VERIFIED claim.

The historical Golden remains the recovery firmware and is not modified by this phase.

## 3. Golden build-time patch facts

The Golden is not reproducible from the repository source tree alone unless its build-time dependency modifications are applied.

### Pico-PIO-USB root ports

The pinned Pico-PIO-USB source contains:

- PIO_USB_ROOT_PORT_CNT = 2.

The G2E3 workflow modifies that value before build to:

- PIO_USB_ROOT_PORT_CNT = 3.

The physical wiring used by OAG is:

- Port 1: D+ GPIO2 / D- GPIO3.
- Port 2: D+ GPIO4 / D- GPIO5.
- Port 3: D+ GPIO6 / D- GPIO7.

src/usbhostmanager.cpp uses the BoardConfig root for GPIO2 and adds extra PIO roots on D+ GPIO4 and D+ GPIO6 via pio_usb_host_add_port.

### TinyUSB HID enumeration quirk

The pinned TinyUSB hidh_set_config implementation begins in CONFG_SET_IDLE.

The G2E3 build runs .github/scripts/patch_oag_tinyusb_hid_host.py before compilation.

That patch skips the optional SET_IDLE stage for VID 2563 / PID 0575 and proceeds directly to report-descriptor acquisition. This is required for the known Redragon G808 / ShanWan receiver behavior.

### V1 architectural decision for dependency patches

Hidden CI-only mutation is a reproducibility risk.

V1 will initially preserve the exact proven behavior but make it explicit and deterministic:

1. Pin dependency SHAs.
2. Keep OAG patches in a dedicated oag/patches area.
3. Verify the exact expected pre-patch source before applying.
4. Make patch application idempotent and fail closed if upstream content differs.
5. Record patch digests in the build manifest.

Longer term, prefer one narrow upstream/fork hook for each requirement:

- Pico-PIO-USB: make root-port count externally configurable instead of hard-coding OAG device knowledge.
- TinyUSB HID host: expose an early-enumeration policy hook so OAG can ask its EarlyQuirkRegistry whether SET_IDLE must be skipped. Device VID/PID knowledge should live in OAG, not inside a third-party dependency.

## 4. Hardware initialization invariant

The current source confirms the proven sequence:

1. TinyUSB device initialization.
2. USBHostManager start.
3. Main loop begins servicing USB Host.
4. UniversalBluetoothHostAddon defers CYW43 initialization.
5. Bluetooth waits 100 ms before cyw43_arch_init.
6. BTstack HID Classic and BLE clients are then initialized.

This ordering is a V1 hardware invariant until isolated hardware tests prove an alternative safe sequence.

V1 will represent this explicitly in BoardRuntime / StartupSequencer rather than relying on addon ordering side effects.

## 5. Dependency map

### Dependencies required or expected for early V1

- Raspberry Pi Pico SDK.
- TinyUSB device stack.
- TinyUSB host stack.
- Pico-PIO-USB.
- pico_cyw43_arch_none when Bluetooth is enabled.
- pico_btstack_cyw43 when Bluetooth is enabled.
- pico_btstack_classic when Bluetooth Classic is enabled.
- pico_btstack_ble when BLE is enabled.
- Standard RP2350 hardware libraries required by the selected transports.

### Dependencies to add only with a concrete feature

- pico_mbedtls: only when a selected authentication implementation genuinely requires it.
- Crypto/authentication helpers: isolated behind auth feature selection.
- Persistent configuration storage support: minimal implementation only when persistence is introduced.

### Legacy dependencies not part of the initial V1 target

Do not pull these into the new target merely because the GP2040 target links or compiles them:

- ArduinoJson.
- nanopb and the full GP2040 protobuf configuration model.
- httpd.
- lwIP/RNDIS Web Config stack.
- ADS1219.
- ADS1256.
- NeoPico.
- OneBitDisplay.
- WiiExtension.
- SNESpad.
- GP2040 display/UI stack.
- Animation station.
- Arcade-oriented addons.
- Unrelated board support.

This is a new executable target, not a feature mode inside the full legacy executable.

## 6. Source extraction classification

### REUSE DIRECTLY

Reuse behavior or self-contained implementation with a thin V1 adapter and without importing unrelated GP2040 managers:

| Component | Decision | Notes |
| --- | --- | --- |
| Pinned TinyUSB revision | REUSE DIRECTLY | With deterministic OAG patch/hook strategy. |
| Pinned Pico-PIO-USB revision | REUSE DIRECTLY | Preserve known 3-root behavior. |
| Proven USB D+/D- wiring | REUSE DIRECTLY | Hardware invariant. |
| USB-before-CYW43 startup ordering | REUSE DIRECTLY | Represent explicitly in StartupSequencer. |
| 100 ms Bluetooth settle behavior | REUSE DIRECTLY | Preserve until isolated hardware test changes it. |
| src/drivers/shared/xinput_host.* | REUSE DIRECTLY behind adapter | Useful proven low-level XUSB/XGIP host class path for first USB milestone. |
| OAG product strings | REUSE DIRECTLY initially | Branding only; platform identities remain driver-owned. |
| Existing license/reference policy | REUSE DIRECTLY | Keep provenance discipline. |

### REFACTOR BEFORE REUSE

| Component | Reason |
| --- | --- |
| UniversalDeviceRegistry | Good classification data, but transport, profile, capabilities and quirks need stronger typed separation and physical-device/interface records. |
| UniversalInputManager | Currently binds transport policy to fixed slots and owns normalized GamepadState directly. |
| UniversalHumanInterfaceManager | Useful NKRO/consumer/mouse state concepts, but fixed Bluetooth/USB slot model must be removed. |
| UniversalOutputManager | Current policy is deterministic Input Slot N -> Output Slot N; mapping must become a separate binding/pipeline layer. |
| UniversalFeedbackManager | Good reverse-channel concept but too narrow: motors only, slot-addressed rather than logical/physical binding aware. |
| UniversalGamepadParser | Protocol-normalization boundary is correct, but current central switch only implements XUSB/XGIP and does not scale as a plugin registry. |
| UniversalXInputHostAddon | Transport, XGIP initialization, slot allocation, parsing and feedback are mixed. Split transport/protocol/device driver roles. |
| UniversalHIDGamepadHostAddon | Descriptor parser, device profiles, quirks, normalized input and rumble encoders are mixed in one module. |
| UniversalHIDHumanInterfaceHostAddon | Descriptor parsing, device aggregation, keyboard LED transport and slot allocation need separation. |
| UniversalBluetoothHostAddon | Large monolith mixing BT transport, discovery, pairing persistence, HID parsing, controller profiles, keyboard/mouse, feedback and slot publishing. |
| Redragon/GigaMax/T29 handling | Preserve exact behavior, move device knowledge into profile/quirk/driver modules. |
| OAGMultiHIDDriver | Valuable PC behavior, but it reads legacy output manager directly and merges physical keyboards/mice before mapping. |
| OAGMultiXInputDriver | Valuable multi-controller descriptor work, but feedback OUT is incomplete as a universal routed path and PC XInput must remain distinct from Xbox consoles. |
| XGIPProtocol | Useful protocol implementation but currently owns large internal buffers and is coupled to XBOne descriptor types. |
| XInputAuth / PS4Auth / XBOneAuth | Existing mechanisms are coupled to GPDriver and USBListener assumptions. Extract provider state machines behind new auth interfaces. |
| Existing PS/Switch/Xbox output code | Protocol knowledge is useful, but arcade/gamepad legacy state and driver lifecycle must not enter the universal core. |
| Build-time patch scripts | Preserve behavior, move to deterministic V1 dependency preparation with hash/precondition checks. |

### REFERENCE ONLY

- GP2040 GPDriver lifecycle as a whole.
- DriverManager.
- AddonManager.
- StorageManager configuration architecture.
- Existing full PS3, PS4, Xbox One, Xbox Original, Switch and Switch Pro drivers until each is deliberately adapted as a V1 Platform Driver.
- Existing PS4 key-mode code unless explicitly selected and reviewed; live passthrough/donor mechanisms are preferred.
- Third-party behavior from BlueRetro and HID-Remapper unless exact licensing/provenance is recorded.
- GIMX behavior only; no direct GPL code reuse under the current project policy.
- Legacy Web Config concepts as possible future UX reference only.

### REMOVE FROM NEW CORE

Do not include in the initial V1 executable:

- GP2040 Web Config application.
- RNDIS/httpd/lwIP configuration stack.
- Display UI and display drivers.
- Animation/RGB subsystems not required for controller LEDs.
- Arcade-only layouts and hotkey machinery.
- SOCD pipeline.
- Legacy Turbo implementation.
- ADC addons unrelated to OAG.
- Wii/SNES/TG16 inputs unless a future product requirement explicitly adds them.
- Multi-board abstraction for boards other than Pico 2 W.
- Legacy profile/protobuf configuration model.
- GP2040 global Storage/Gamepad ownership model.

## 7. Important findings in the current G2E3 universal path

### Fixed transport-to-slot coupling

Current UniversalInputManager reserves:

- Slot 0 for Bluetooth.
- Slots 1..3 for USB.

That solved G2E3 coexistence but must not survive as a V1 architectural rule.

### Fixed input-to-output coupling

Current UniversalOutputManager uses a 1:1 policy:

- Input Slot N -> Output Slot N.

V1 replaces this with a BindingGraph / LogicalSlotManager.

### Current GamepadState is legacy-shaped

The current GamepadState uses:

- 32-bit buttons.
- 16-bit unsigned axes centered at 0x7FFF.
- 8-bit triggers.
- GP2040-specific naming.

V1 will not use GamepadState as its universal internal ABI.

### Current feedback is a useful proof

OAGMultiHIDDriver can publish per-interface host feedback into UniversalFeedbackManager, and physical HID/XInput/BT host paths already contain rumble transmission logic.

The concept is proven and should be retained, but addressing and capability modeling must be generalized.

### OAGMultiXInput feedback gap

The current OAGMultiXInput custom class driver continuously arms OUT endpoints but does not yet convert the received OUT payload into UniversalFeedbackManager events.

Therefore OAGMultiXInput is a PC output reference, not a complete V1 feedback implementation.

## 8. Target architecture

The V1 runtime is divided into nine independent layers.

### Layer A — HAL / Board Runtime

Responsibilities:

- RP2350 clock policy.
- Pico 2 W board pins.
- monotonic time.
- critical sections / queues.
- flash configuration storage primitives.
- deterministic startup ordering.
- no controller protocol knowledge.

### Layer B — Transport

Responsibilities:

- USB PIO host.
- Bluetooth Classic.
- Bluetooth LE.
- future 2.4 GHz transport adapters where the receiver is not simply USB HID/XUSB.
- discovery/connect/disconnect.
- raw transport reports.
- raw transport writes.

Transport does not parse Xbox, PlayStation or Nintendo semantics.

### Layer C — Device Discovery / Registry

Responsibilities:

- Physical device records.
- Interface records for composite devices.
- VID/PID and Bluetooth identity.
- descriptors/signatures.
- profile selection.
- protocol-driver selection.
- typed capability sets.
- quirks.
- authentication roles.
- connection lifecycle.
- generation-safe DeviceId.

### Layer D — Protocol Drivers

Responsibilities:

- Parse physical-device reports.
- emit universal state.
- encode physical feedback.
- perform protocol-specific controller initialization.
- no target-platform output policy.

Examples:

- XusbInputDriver.
- XgipInputDriver.
- GenericHidGamepadDriver.
- HidKeyboardDriver.
- HidMouseDriver.
- XboxBleInputDriver.
- Ds4InputDriver.
- DualSenseInputDriver.
- SwitchProInputDriver.

### Layer E — Universal Physical Input State

Per-device normalized snapshots:

- gamepad.
- keyboard.
- mouse.
- future extension states.

No platform descriptor or console identity exists here.

### Layer F — Binding + Mapping Pipeline

Responsibilities:

- DeviceId -> LogicalSlotId.
- multiple physical inputs -> one logical controller.
- one physical input -> multiple logical actions.
- remapping.
- combos.
- macros.
- sequences.
- timing/layers/profiles.

U1 implements only PassThroughMappingStage.

### Layer G — Universal Logical Output State

Logical controller/keyboard/mouse state produced after mapping.

Platform drivers consume only this state.

### Layer H — Platform Output + Authentication

PlatformOutputManager owns selected platform driver instances.

Examples:

- PC Generic HID.
- PC XInput-compatible.
- Nintendo Switch.
- PS3.
- PS4.
- PS5.
- Xbox 360 console.
- Xbox One console.
- Xbox Series console.
- Android profiles.
- iOS/iPadOS profiles.

Authentication is requested through AuthBroker / IPlatformAuthProvider, not hidden inside input parsers.

### Layer I — Feedback Router

Platform feedback is normalized and routed back through logical bindings to physical device drivers.

This is independent of the forward input path.

## 9. Ownership model

Avoid singleton-heavy architecture.

A top-level CoreContext owns all long-lived objects using static/fixed storage:

- BoardRuntime.
- TransportRegistry.
- DeviceRegistry.
- ProtocolDriverRegistry.
- LogicalSlotManager.
- MappingPipeline.
- LogicalOutputStore.
- PlatformOutputManager.
- AuthBroker.
- FeedbackRouter.
- Diagnostics.

No heap allocation is allowed in the steady-state input loop.

Runtime object ownership is explicit and destruction/reconnect uses generation-safe handles.

## 10. Proposed folder structure

oag/
- CMakeLists.txt
- cmake/
  - OagFeatures.cmake
  - OagToolchainChecks.cmake
- config/
  - default_features.h.in
  - build_profiles.cmake
- include/oag/
  - core/
  - hal/
  - transport/
  - device/
  - protocol/
  - input/
  - mapping/
  - output/
  - auth/
  - config/
  - diagnostics/
- src/
  - core/
  - hal/rp2350/
  - transport/usb_pio/
  - transport/bluetooth/
  - device/
  - protocol/hid/
  - protocol/xusb/
  - protocol/xgip/
  - protocol/playstation/
  - protocol/nintendo/
  - input/
  - mapping/
  - output/platforms/pc/
  - output/platforms/switch/
  - output/platforms/playstation/
  - output/platforms/xbox/
  - output/platforms/mobile/
  - auth/
  - config/
  - diagnostics/
- patches/
  - pico_pio_usb/
  - tinyusb/
- tests/
  - host/
  - firmware/
  - vectors/
- tools/
  - prepare_dependencies.py
  - check_architecture.py
  - size_report.py
  - generate_build_manifest.py
- docs/
  - architecture/
  - adr/

The existing GP2040 target remains intact while the new oag target grows independently.

## 11. Core interfaces

The exact signatures can evolve in U1, but dependencies must point in these directions.

### ITransport

Conceptual operations:

- start.
- stop.
- poll.
- submitOutput.
- controlTransfer where required.
- report connection/disconnection/raw data to an ITransportSink.

Transport receives opaque TransportHandle values. It does not expose logical slots.

### ITransportSink

Receives:

- onDeviceDiscovered.
- onDeviceDisconnected.
- onRawReport.
- onTransportEvent.

### IProtocolDriver

Operations:

- match descriptor/profile.
- attach to a DeviceContext.
- detach.
- process raw input.
- apply universal feedback.
- periodic poll only when the protocol requires it.

Protocol drivers emit universal state through a small state sink.

### IPlatformOutputDriver

Operations:

- platformId.
- capabilities.
- initialize.
- submit logical output snapshots.
- poll.
- consume platform-host feedback.
- declare authentication requirement.

A platform driver never asks a USB input transport directly for gameplay state.

### IPlatformAuthProvider

Operations:

- provider capabilities.
- canSatisfy AuthRequirement.
- bind to an eligible DeviceId or external source.
- begin.
- poll.
- handle platform auth request/challenge.
- expose status.
- release.

Provider data is not interpreted by Universal Input.

### IMappingStage

Operations:

- reset.
- process InputSnapshotSet -> LogicalOutputBuilder.
- tick with deterministic monotonic time.

The first implementation is pass-through.

## 12. Transport model

Use transport-independent physical identities.

TransportType:

- UsbPioHost.
- BluetoothClassic.
- BluetoothLe.
- WirelessReceiver.
- ExternalAuth.

TransportHandle contains only transport-local addressing.

For USB this includes device address and interface number.

For Bluetooth it includes connection handle/address metadata.

The DeviceRegistry translates a TransportHandle into a generation-safe DeviceId.

A USB 2.4 GHz receiver can still be TransportType UsbPioHost while the profile records that the physical product is a wireless receiver. Do not create a fake transport type when the actual transport to the Pico is normal USB.

## 13. Early enumeration quirk model

Some quirks are needed before a normal DeviceRecord exists.

Example: Redragon/ShanWan SET_IDLE avoidance occurs inside HID enumeration.

Create EarlyQuirkRegistry keyed by information available at enumeration time:

- transport.
- VID/PID.
- interface class/subclass/protocol.
- optional descriptor signature when available.

TinyUSB/Pico-PIO integration can ask this table through one narrow hook.

This avoids scattering vendor conditions across third-party code.

## 14. Device Registry model

### DeviceId

Use a generation-safe handle:

- index: uint16_t.
- generation: uint16_t.

A stale DeviceId becomes invalid after disconnect/reuse.

Do not derive DeviceId from port number, USB address or Bluetooth slot.

### PhysicalDeviceRecord

Contains:

- DeviceId.
- connection state.
- TransportType.
- TransportHandle.
- VID/PID where applicable.
- Bluetooth identifiers where applicable.
- selected DeviceProfileId.
- selected protocol driver.
- quirks.
- capability sets.
- authentication role capabilities.
- platform affinity hints.
- timestamp/generation metadata.

### InterfaceRecord

Composite devices may expose keyboard, consumer control, mouse, gamepad and authentication interfaces simultaneously.

The physical record therefore owns multiple InterfaceRecords instead of treating every interface as an unrelated physical device.

## 15. Capability model

Do not use one flat uint32 for every concept.

Use separate typed bitsets:

InputCapabilities:
- Gamepad.
- Keyboard.
- Mouse.
- AnalogTriggers.
- Touchpad.
- Motion.
- Battery.
- ConsumerKeys.

FeedbackCapabilities:
- RumbleLeftRight.
- TriggerRumble.
- PlayerLeds.
- RgbLightbar.
- ControllerLeds.
- AdaptiveTriggers.
- KeyboardLeds.
- future Audio flag only.

AuthCapabilities:
- Xbox360Donor.
- XboxGipDonor.
- PlayStationDonor.
- UsbPassthrough.
- ExternalAuth.
- platform-specific extension bits.

Also maintain a VerifiedCapabilities subset where hardware verification is meaningful.

## 16. Universal Gamepad model

V1 should not inherit the 8-bit-trigger and GP2040 button ABI.

Recommended core state:

- uint64_t buttons.
- compact D-pad bits or enum.
- int32_t lx.
- int32_t ly.
- int32_t rx.
- int32_t ry.
- uint32_t lt.
- uint32_t rt.
- DeviceId source.
- uint8_t playerIndex.
- connection flags.
- uint32_t generation.
- uint32_t timestampUs.
- capability-linked optional extension state.

Axes use a signed canonical full-range representation with zero center.

Triggers use an unsigned canonical full-range representation.

Protocol drivers normalize once into the canonical range; platform drivers normalize again only to the destination report width.

Touch, motion and battery are extensions rather than forcing large unused fields into every minimal gamepad snapshot.

## 17. Universal Keyboard model

Keep the proven concepts:

- 256-bit usage bitmap.
- modifier byte.
- fixed-capacity consumer usage set.
- DeviceId.
- connection state.
- generation.
- timestamp.
- LED state metadata.

Keyboard LED writes are emitted through Universal Feedback, even if the keyboard snapshot exposes the current LED metadata for observability.

No boot-protocol six-key rollover assumption is allowed in the universal state.

## 18. Universal Mouse model

State:

- int32_t x.
- int32_t y.
- int32_t wheel.
- int32_t horizontalWheel.
- uint32_t buttons.
- relative/absolute mode.
- DeviceId.
- connection state.
- generation.
- timestamp.

Mouse deltas are consumed exactly once by the mapping/output pipeline. Accumulators must use saturating arithmetic and must not silently wrap.

## 19. Logical slot model

Current product target:

- up to four logical gamepad outputs.

Architecture:

DeviceId -> BindingGraph -> LogicalSlotId.

Transport has no reserved logical slot.

The same SlotManager works for USB and Bluetooth.

Future mappings can support:

- many physical devices -> one logical gamepad.
- one physical device -> multiple logical actions.
- keyboard/mouse -> a gamepad slot.
- separate keyboard/mouse logical outputs.

The default U1 binding policy is first-connected deterministic allocation, not last-active arbitration.

## 20. Mapping / Combo / Macro boundary

The mapping engine sits strictly between physical normalized state and logical output state.

It does not live inside USB, Bluetooth or platform drivers.

Pipeline:

PhysicalInputStore -> BindingGraph -> MappingPipeline -> LogicalOutputStore.

U1 implementation:

- PassThroughMappingStage only.

Future stages:

- ButtonMapStage.
- AxisTransformStage.
- MouseAimStage.
- ComboStage.
- MacroStage.
- LayerStage.
- ConditionalStage.

Timing uses one monotonic clock service and deterministic state machines. No sleep-based macro implementation is permitted.

## 21. Universal Feedback model

Recommended normalized feedback state:

- uint16_t leftMotor.
- uint16_t rightMotor.
- uint16_t leftTriggerMotor.
- uint16_t rightTriggerMotor.
- player LED mask/index.
- RGB/lightbar value and validity.
- controller LED state.
- keyboard LED state.
- adaptive-trigger descriptor extension.
- future audio capability flag only.
- generation/timestamp.

Flow:

PlatformOutputDriver -> FeedbackRouter -> logical target -> BindingGraph -> DeviceId -> IProtocolDriver.applyFeedback -> ITransport.submitOutput.

Unsupported feedback fields are capability-filtered rather than silently interpreted as something else.

## 22. Platform Output strategy

Platform and protocol names must remain precise.

PC:

- PC Generic HID profile.
- PC XInput/XUSB-compatible profile.
- PC keyboard.
- PC mouse.
- optional future debug/WebHID profile.

Xbox:

- Xbox 360 console driver.
- Xbox One console driver.
- Xbox Series console driver.
- Xbox Original/XID only if required.

PlayStation:

- PS3 driver.
- PS4 driver.
- PS5 driver.

Nintendo:

- Switch driver as its own platform plugin.

Mobile:

- Android profile(s).
- iOS/iPadOS profile(s).

PC XInput is not Xbox console support.

Generic HID is not a universal console output protocol.

## 23. Authentication strategy

Authentication is an independent service.

AuthBroker receives an AuthRequirement from the selected PlatformOutputDriver.

It chooses a provider based on:

- platform.
- required mechanism.
- connected device AuthCapabilities.
- configured preference.
- provider availability.

Supported architectural provider categories:

- NoAuthProvider.
- UsbPassthroughAuthProvider.
- DonorControllerAuthProvider.
- ExternalAuthDeviceProvider.
- PlatformSpecificAuthProvider.

An auth donor can be different from the gameplay input device.

A donor device can remain registered as a physical device while one of its roles is claimed by AuthBroker.

No provider is tied to P1/P2/P3.

Only legitimate live passthrough/donor/external mechanisms are in scope. Extracting, cloning or fabricating platform secrets is out of scope.

## 24. Existing authentication code decision

Existing GPAuthDriver, XInputAuth, PS4Auth and XBOneAuth demonstrate useful state machines and host-listener flows.

They are not adopted as the new interface because:

- they are coupled to GPDriver lifecycle.
- they assume legacy Storage/Gamepad configuration.
- USBListener is used as the provider binding mechanism.
- some driver behavior blocks gameplay until auth state is ready.
- input/auth device role separation is incomplete.

V1 will extract protocol-specific auth state machines only after AuthBroker and IPlatformAuthProvider exist.

## 25. Platform selection architecture

BootProfile contains:

- PlatformId.
- OutputProfileId.
- MappingProfileId.
- AuthPolicy.
- enabled runtime feature set.

Selection precedence:

1. Explicit boot button combination.
2. Valid persisted profile.
3. compile-time default.
4. optional future auto-detection when the target protocol supports reliable detection.

Auto-detection must not silently rewrite persistent configuration.

U1 uses a compile-time/default manual PC profile only.

## 26. Configuration architecture

Do not import GP2040 protobuf/Web Config for V1.

Use a small versioned binary configuration record:

- magic.
- schema version.
- record length.
- generation.
- CRC.
- BootProfile.
- small mapping-profile reference fields.

When writes are introduced, use dual-slot A/B records or another atomic strategy so power loss cannot destroy the last valid configuration.

U1 can run entirely from compiled defaults; persistence is not required to prove the core architecture.

## 27. Build / feature configuration

Use feature groups rather than dozens of unrelated preprocessor switches.

Examples:

- OAG_FEATURE_USB_HOST.
- OAG_FEATURE_BLUETOOTH.
- OAG_FEATURE_KEYBOARD.
- OAG_FEATURE_MOUSE.
- OAG_FEATURE_MAPPING.
- OAG_FEATURE_AUTH.
- OAG_PLATFORM_PC.
- OAG_PLATFORM_SWITCH.
- OAG_PLATFORM_PLAYSTATION.
- OAG_PLATFORM_XBOX.

CMake generates one oag_build_config.h.

Named build profiles provide sane combinations:

- usb-core.
- usb-pc.
- usb-bt-pc.
- switch-dev.
- ps-dev.
- xbox-dev.

Do not compile every platform driver into every artifact.

## 28. Flash / RAM strategy

Raspberry Pi Pico 2 W provides 4 MB on-board flash and RP2350 provides 520 KB SRAM.

These are architectural ceilings, not permission to fill the device.

### Initial V1 budgets

USB-only PC development build:

- Flash warning target: 768 KiB.
- Flash hard U1/U2 ceiling: 1024 KiB.
- Static data + BSS warning target: 160 KiB.
- Static data + BSS hard ceiling: 200 KiB.

USB + Bluetooth default product-direction build:

- Flash warning target: 1280 KiB.
- Flash hard ceiling: 1536 KiB.
- Static data + BSS warning target: 260 KiB.
- Static data + BSS hard ceiling: 300 KiB.

Project-wide reserve policy:

- Keep at least 2 MB flash uncommitted during early architecture growth.
- Keep at least about 160 KB SRAM uncommitted for stacks, BT/CYW43 runtime, endpoint buffers, future auth, transient descriptor work and safety margin.

These are initial gates and must be replaced by measured data as soon as the new target builds.

### Runtime memory rules

- fixed-capacity registries.
- no steady-state heap allocation.
- bounded descriptor caches.
- bounded protocol queues.
- no unbounded STL containers in runtime paths.
- stack-usage reporting in Debug CI.
- large crypto/auth buffers exist only in builds that need them.
- platform-specific large tables do not enter unrelated build profiles.

## 29. Build strategy

Create a separate CMake entry at oag/CMakeLists.txt.

Build example concept:

- source directory: oag.
- board: pico2_w.
- platform: rp2350-arm-s.
- output executable: OAG-Universal-Core-V1.

The legacy root GP2040-CE executable remains untouched.

The new target explicitly links only required libraries.

Dependency preparation must verify:

- TinyUSB SHA.
- Pico-PIO-USB SHA.
- patch preconditions.
- patch digest/result.
- Pico SDK version.

Each build emits a machine-readable manifest.

## 30. Testing strategy

Use the project verification vocabulary exactly.

### SOURCE EXISTS

A source implementation is present and inspectable.

### SOFTWARE VERIFIED

Host tests/static checks/build tests have passed for the relevant scope.

### CI VERIFIED

The exact commit has passed the required CI workflow.

### HARDWARE VERIFIED

Only the user can promote a milestone to this state after physical device testing.

### Test layers

1. Host-native core tests.
2. Parser vector tests.
3. Descriptor parser tests.
4. Registry/profile matching tests.
5. Binding/slot tests.
6. Mapping deterministic timing tests.
7. Feedback routing tests.
8. Auth-provider state-machine tests using mock transports.
9. Firmware build tests.
10. Hardware gates.

Fuzz/property testing should be added for HID descriptor/report parsing because malformed descriptors must not corrupt fixed pools or overrun buffers.

## 31. Architecture static checks

oag/tools/check_architecture.py should fail CI on prohibited dependencies.

Examples:

- core cannot include Pico SDK.
- protocol cannot include platform-output headers.
- transport cannot include platform driver headers.
- input cannot include auth provider headers.
- mapping cannot include TinyUSB/BTstack.
- platform drivers cannot include physical USB/Bluetooth host implementation.
- auth cannot mutate physical gameplay state.
- vendor VID/PID tables exist only in device/profile/quirk modules or approved early-enumeration tables.

## 32. CI strategy

Add a dedicated workflow in U1, not U0.

Suggested workflow name:

OAG Universal Core V1

Trigger:

- push to chatgpt/oag-universal-core-v1.
- workflow_dispatch.

Jobs:

1. source-policy.
2. host-unit-tests.
3. dependency-prepare.
4. pico2w-release-build.
5. size-gate.
6. artifact-manifest.
7. upload UF2/ELF/MAP/manifest.

Do not depend on the legacy Web Config Node job.

Do not change Golden or Antigravity workflow ownership.

## 33. Artifact naming and provenance

Suggested development artifact:

OAG_Universal_Core_V1_<phase>_<shortsha>_Pico2W.uf2

Also emit:

- ELF.
- MAP.
- build-manifest.json.
- size-report.txt.

Manifest fields:

- full Git SHA.
- branch.
- build time.
- Pico SDK version.
- TinyUSB SHA.
- Pico-PIO-USB SHA.
- patch digests.
- enabled features.
- selected platform profile.
- text/data/bss sizes.
- UF2 digest.

A filename alone is not a reproducibility record.

## 34. Migration plan from G2E3

Migration is extraction by vertical slice, never a bulk copy.

### U1 — Core scaffold + first USB hardware slice

Create the independent target and minimum interfaces.

Extract only:

- board startup primitives.
- 3-port PIO USB host behavior.
- XInput low-level host support required for T29/XUSB.
- DeviceRegistry minimum.
- DeviceId.
- LogicalSlotManager.
- UniversalGamepadState.
- PassThroughMappingStage.
- one PC debug/generic HID output profile.

No Bluetooth, no keyboard/mouse, no rumble, no auth.

### U2 — Generic HID + human interfaces + feedback

Extract/refactor:

- Generic HID gamepad descriptor parser.
- Redragon/GigaMax profiles.
- keyboard/NKRO/consumer.
- mouse.
- keyboard LEDs.
- universal feedback.
- physical rumble encoders.
- PC multi-HID or refined PC output.

Hardware gates are split by device family.

### U3 — Bluetooth transport and profiles

Split the G2E3 Bluetooth monolith into:

- BT runtime/transport.
- pairing/persistence.
- BLE HID transport adapter.
- Classic HID transport adapter.
- generic HID parser reuse.
- Xbox BLE profile.
- DS4.
- DualSense.
- Switch Pro.
- BT feedback.

Preserve USB-before-CYW43 and settle timing.

### U4 — Mapping foundation

Add:

- BindingGraph configuration.
- button remap.
- keyboard/mouse -> gamepad basics.
- timing service.
- layers/profile skeleton.

Do not jump directly to a large macro language.

### U5 — Platform plugin extraction

Adapt one platform at a time:

- Switch.
- PS3/PS4.
- Xbox families.
- mobile profiles.

Each gets its own CI and hardware gate.

### U6 — Authentication broker/providers

After platform driver boundaries are stable:

- donor discovery.
- passthrough provider.
- platform-specific providers.
- role arbitration between gameplay and auth use.

### U7 — Configuration and product hardening

- persistent profiles.
- diagnostics.
- recovery behavior.
- size optimization.
- reconnect stress.
- fault injection.
- long-duration soak testing.

## 35. First hardware milestone

Milestone name:

U1-HW1 — Three-Port XUSB New-Core Proof

Scope is intentionally small.

Required behavior:

- Build the new independent OAG target.
- Initialize the three proven PIO USB roots.
- No Bluetooth in this artifact.
- Detect the T29/XUSB-compatible controller through the new DeviceRegistry.
- Assign a generation-safe DeviceId.
- Bind it to a logical gamepad slot through LogicalSlotManager.
- Parse XUSB into the new UniversalGamepadState.
- Pass it through PassThroughMappingStage.
- Output one PC Generic HID gamepad through the new PlatformOutputDriver boundary.
- Repeat the same controller test on P1, P2 and P3 without code or profile changes.
- Disconnect/reconnect must invalidate the old DeviceId generation and recover cleanly.

Explicitly out of this first hardware gate:

- rumble.
- Bluetooth.
- keyboard/mouse.
- Generic HID gamepads.
- Xbox console output.
- PlayStation output.
- Switch output.
- authentication.
- macros.

This proves the architecture before stacking additional risk.

## 36. Risk register

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Hidden CI-only dependency mutation | Critical | Deterministic patch preparation, precondition/hash checks, manifest. |
| USB Host / CYW43 startup race | Critical | Explicit StartupSequencer and preserved 100 ms rule; isolated hardware gate. |
| PIO root-port resource assumptions | High | Pin dependency; compile checks; test all three physical ports. |
| TinyUSB HID SET_IDLE receiver regression | High | EarlyQuirkRegistry + narrow host hook; Redragon hardware regression gate. |
| Transport equals logical slot | High | Generation-safe DeviceId + independent LogicalSlotManager. |
| Input slot equals output slot | High | BindingGraph + MappingPipeline boundary. |
| Composite USB devices | High | PhysicalDeviceRecord plus InterfaceRecord model. |
| Two host drivers claim same interface | High | Match score/claim arbitration in ProtocolDriverRegistry. |
| Bluetooth monolith consumes excess RAM | High | Split modules, fixed pools, measured descriptor-cache budget. |
| Feedback reaches wrong physical controller | Critical | Route by logical binding to DeviceId, never by raw platform slot alone. |
| Stale reconnect handles | High | index+generation DeviceId. |
| HID parser malformed descriptor | High | strict bounds + vector tests + fuzz/property tests. |
| Xbox/PS auth tightly coupled to gameplay | Critical | AuthBroker with independent donor role. |
| Unauthorized secret handling | Critical | Live passthrough/donor/external only; no secret extraction/copying. |
| Platform descriptor explosion | High | Compile selected platform profiles; no all-platform composite firmware. |
| Flash growth | High | per-commit size report and hard gates. |
| SRAM fragmentation | High | fixed pools; no steady-state heap. |
| Large crypto buffers/stack | High | auth-only feature builds and dedicated memory gate. |
| Existing legacy platform driver drags arcade logic | Medium | adapt protocol/output portions only. |
| Third-party license contamination | High | provenance file and per-source license check. |
| Antigravity parallel changes | Medium | never auto-merge; compare only when explicitly useful. |
| Local/remote branch confusion | Critical | before every write verify target branch and HEAD; writes only to chatgpt/oag-universal-core-v1. |

## 37. U1 expected initial files

The exact list may shrink during implementation, but U1 is expected to introduce approximately:

- oag/CMakeLists.txt
- oag/cmake/OagFeatures.cmake
- oag/include/oag/core/core_context.h
- oag/include/oag/core/types.h
- oag/include/oag/hal/board_runtime.h
- oag/include/oag/transport/transport.h
- oag/include/oag/transport/transport_sink.h
- oag/include/oag/device/device_id.h
- oag/include/oag/device/device_registry.h
- oag/include/oag/device/device_profile.h
- oag/include/oag/protocol/protocol_driver.h
- oag/include/oag/input/gamepad_state.h
- oag/include/oag/mapping/logical_slot_manager.h
- oag/include/oag/mapping/mapping_stage.h
- oag/include/oag/output/platform_output_driver.h
- oag/src/hal/rp2350/board_runtime.cpp
- oag/src/transport/usb_pio/usb_pio_transport.cpp
- oag/src/device/device_registry.cpp
- oag/src/protocol/xusb/xusb_input_driver.cpp
- oag/src/mapping/logical_slot_manager.cpp
- oag/src/mapping/pass_through_mapping.cpp
- oag/src/output/platforms/pc/pc_hid_output_driver.cpp
- oag/src/core/core_context.cpp
- oag/src/main.cpp
- oag/tools/prepare_dependencies.py
- oag/tools/check_architecture.py
- .github/workflows/oag-universal-core-v1.yml

Do not create placeholders for future platforms merely to make the tree look complete.

## 38. U1 entry criteria

U1 may begin only after this U0 report is reviewed.

Entry criteria:

- Golden commit remains unchanged.
- Development branch contains only accepted U0 documentation changes beyond Golden.
- No Antigravity merge has occurred.
- Architecture boundaries in this report are accepted.
- First hardware milestone remains U1-HW1 unless explicitly revised.
- U1 starts from a fresh fetch of chatgpt/oag-universal-core-v1.
- Dependency SHAs are re-verified.
- Dedicated OAG CI workflow is created before the first firmware milestone is called CI VERIFIED.
- No U1 artifact is called HARDWARE VERIFIED until the user physically validates it.

## 39. U0 verification status

Golden source facts: SOURCE EXISTS.  
Golden exact SHA build workflow: CI VERIFIED by successful GitHub Actions runs.  
Golden hardware behavior: historical HARDWARE VERIFIED only where previously confirmed by the user; U0 does not create new hardware claims.  
OAG Universal Core V1 firmware: not started.  
U0 architecture document: source/design only.  

No firmware binary is built in U0 because U0 intentionally changes no firmware source or build configuration.

## 40. U0 gate result

No architectural blocker was found.

The Golden contains sufficient proven material to start a clean V1 extraction without depending on the Legacy branch.

The highest-priority architectural corrections are:

1. eliminate transport-to-slot coupling.
2. eliminate input-slot-to-output-slot coupling.
3. move to generation-safe DeviceId.
4. split transport from protocol parsing.
5. split physical input from logical mapping.
6. make feedback a reverse routed path.
7. isolate authentication through AuthBroker/providers.
8. make dependency patches deterministic and visible.
9. build a separate Pico 2 W focused executable.
10. enforce memory/size/architecture gates from the first firmware phase.

Next gate:

U0 ACCEPTANCE GATE.

Do not start U1 until the user reviews and accepts this report.
