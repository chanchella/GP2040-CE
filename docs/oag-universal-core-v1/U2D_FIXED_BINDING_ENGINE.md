# U2D — Fixed-capacity Binding/Remap Engine

This phase adds the first configurable remapping primitive inspired by the
lookup-table workflow used by HID remappers, while remaining native to OAG.

## Properties

- No heap allocation.
- Fixed capacity: 64 digital bindings.
- One source may map to multiple targets.
- Duplicate source+target inserts are idempotent.
- Keyboard HID usages and mouse buttons are supported as sources.
- Logical gamepad buttons, D-pad, digital stick directions and triggers are
  supported as targets.
- Opposing stick directions resolve deterministically to neutral.
- Existing/base analog values are preserved when no mapped digital direction
  is active.

## Why this is separate from profiles

The engine is intentionally policy-free. It does not hard-code WASD, mouse
buttons or any player's preferred layout. A later Profile/Config layer will
populate bindings and persist them to flash/WebHID configuration.
