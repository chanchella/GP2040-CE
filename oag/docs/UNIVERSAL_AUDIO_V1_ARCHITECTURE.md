# OAG Universal Audio V1 — Architecture Gate

Baseline: `19b84f3c458384f75a44cc394da8269e49e57340`
Source: `golden/ui5k-oag-abo-gemi-ultra-gaming-v1`
Rule: the Diamond is read-only. Audio work happens only on `chatgpt/oag-universal-audio-v1`.

## Goal

Add a bidirectional audio plane beside the existing Universal Input/Output plane:

Host/console audio OUT -> OAG audio router -> selected headset speaker
Selected headset microphone -> OAG audio router -> host/console microphone IN

The audio plane must not change the proven gamepad, keyboard, mouse, USB Host,
Bluetooth HID, rumble, pairing, or output-profile behavior.

## Transport drivers

1. USB device audio (first milestone)
   - Composite UAC2 headset function on the target-facing USB device.
   - Stereo playback: 48 kHz, signed PCM16, 2 channels.
   - Microphone capture: 48 kHz, signed PCM16, mono.
   - Existing PC XInput receiver + keyboard + mouse interfaces remain present.
   - New audio endpoints must use currently unused endpoint numbers.

2. Bluetooth headset
   - High-quality playback path: Bluetooth Classic A2DP Source.
   - Bidirectional headset/microphone path: HFP Audio Gateway.
   - HFP is the required path when microphone capture is active.
   - Audio routing is independent of Bluetooth HID controller peers.

3. Direct wired headset
   - USB digital headsets require USB Audio Host/isochronous support.
   - Analog 3.5 mm TRRS requires an external audio codec/headset front end;
     Pico 2 W alone does not provide the required stereo DAC, microphone bias,
     headset amplification, and jack interface.

4. Controller headset ports
   - Driver-specific capability; never assume audio from the presence of a jack.
   - PlayStation USB audio-capable controllers get a dedicated audio driver.
   - Xbox controller/accessory audio is isolated behind a GIP/XUSB audio driver.
   - Unsupported controllers remain fully functional as gamepads.

## Core model

AudioEndpoint: transport, playback/capture/duplex, format, rate, channels, state.
AudioRouter: explicit playback/capture routes with bounded ring buffers.
AudioTransportDriver: probe, capabilities, start/stop, PCM push/pull, recovery.

## Stability gates

- PIO USB Host initializes before Bluetooth/CYW43 exactly as Golden.
- Existing gamepad slots, Primary rules, rumble and pairing remain unchanged.
- No Last-Active arbitration.
- Audio callbacks never block HID/XInput loops.
- Audio disconnect releases only audio resources.
- Bluetooth audio cannot evict controller HID peers.
- USB audio changes require interface/endpoint/DPRAM budget guards.
- Golden branch/ref is never modified.

## Milestones

A1 — USB Audio Device Enumeration
- PC sees OAG Headphones + OAG Microphone.
- Four XInput outputs + keyboard + mouse still enumerate.
- Playback is accepted into a bounded sink; microphone emits silence initially.

A2 — USB Audio Runtime
- Stable playback/capture clocks, bounded buffers, underrun/overrun counters.
- Diagnostic loopback.

A3 — Bluetooth Headset Full Duplex
- HFP Audio Gateway + SCO bridge to USB audio.

A4 — Bluetooth High-Quality Playback
- A2DP Source.
- Explicit A2DP <-> HFP transition when microphone is requested.

A5 — Wired Audio
- External I2S codec for analog TRRS.
- USB Audio Host investigation for digital USB headsets.

A6 — Controller Audio
- PlayStation controller audio driver.
- Xbox GIP/XUSB audio driver.
- Capability registry per protocol/device.

## First implementation target

A1 only. The first hardware gate is Windows enumerating OAG Headphones and
OAG Microphone while all Golden controller/keyboard/mouse behavior remains
unchanged.
