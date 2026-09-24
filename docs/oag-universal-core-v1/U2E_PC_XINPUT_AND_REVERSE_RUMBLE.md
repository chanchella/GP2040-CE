# U2E — PC XInput Device Output + Reverse Rumble Runtime

U2E replaces the temporary PC Generic HID device output with a custom
Xbox 360/XInput-compatible TinyUSB device driver.

## PC device-side path

```
LogicalGamepadState
  -> XinputReportEncoder
  -> persistent 20-byte XInput report
  -> interrupt IN endpoint 0x81
  -> Windows XInput stack
```

The output supports the basic Xbox 360 gameplay model:

- A/B/X/Y;
- D-pad;
- Start/Back;
- LB/RB;
- L3/R3;
- Guide;
- LT/RT analog triggers;
- LX/LY/RX/RY signed 16-bit sticks.

The canonical OAG Y-axis convention is converted back to XInput wire
orientation at the final encoder boundary.

## Reverse feedback

```
Windows OUT endpoint
  -> XinputFeedbackDecoder
  -> RumbleCommand
  -> primary logical slot
  -> DeviceRegistry
  -> physical XUSB handle
  -> tuh_xinput_send_report()
  -> T29
```

The latest rumble command is retained while the physical XUSB OUT endpoint is
temporarily busy, then retried until it is submitted. If no XUSB primary
controller exists, the stale rumble command is discarded.

## USB identity

The PC compatibility profile uses the Xbox 360 controller VID/PID and
descriptor layout required for Windows XInput binding, while OAG supplies its
own manufacturer/product strings and a serial generated from the Pico unique
board ID.

This is a PC compatibility profile. It does not make OAG an official
Microsoft controller and does not implement or fabricate console
authentication credentials.

## Preserved input side

U2E keeps:

- three PIO USB roots;
- RP2350-E9-capable Pico-PIO-USB backend;
- two-second root reconciliation watchdog;
- XUSB/T29 host input;
- Boot Keyboard host input;
- Boot Mouse host input.

## Hardware acceptance

1. Windows enumerates OAG as an XInput/Xbox 360-compatible controller.
2. `joy.cpl` / gamepad tester shows all basic buttons, D-pad, triggers and
   four stick axes.
3. T29 input reaches Windows through OAG.
4. Windows/XInput rumble is forwarded back to T29.
5. Existing P1/P2/P3 behavior is checked again without assuming the hot-plug
   gate is solved.
