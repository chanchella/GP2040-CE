#include <Arduino.h>
#include <JoystickBLE.h>

// OAG UI5K Bluetooth -> Windows diagnostic probe.
//
// IMPORTANT:
// This is an isolated diagnostic firmware built from a branch forked directly
// from the UI5K Golden commit. It does NOT modify or execute the UI5K runtime.
//
// Target:
//   Raspberry Pi Pico 2 W -> BLE HID Gamepad -> Windows
//
// Windows Bluetooth name:
//   OAG PC BLE GAMEPAD
//
// Automatic repeating self-test:
//   1. Button 1 DOWN
//   2. Button 1 UP
//   3. D-Pad DOWN
//   4. D-Pad NEUTRAL
//   5. Left X FULL RIGHT
//   6. Left X CENTER
//
// Expected validation:
//   Windows Settings -> Bluetooth & devices -> Add device -> Bluetooth
//   Pair "OAG PC BLE GAMEPAD"
//   Win+R -> joy.cpl -> Properties
//   Observe the repeating self-test.

static constexpr uint32_t kStartupDelayMs = 2500;
static constexpr uint32_t kStepIntervalMs = 1200;

uint32_t nextStepMs = 0;
uint8_t stepIndex = 0;

void setNeutral() {
  JoystickBLE.button(1, false);
  JoystickBLE.hat(-1);

  JoystickBLE.X(512);
  JoystickBLE.Y(512);
  JoystickBLE.Z(512);
  JoystickBLE.Zrotate(512);

  JoystickBLE.sliderLeft(0);
  JoystickBLE.sliderRight(0);
}

void applyStep(uint8_t step) {
  switch (step) {
    case 0:
      JoystickBLE.button(1, true);
      break;

    case 1:
      JoystickBLE.button(1, false);
      break;

    case 2:
      JoystickBLE.hat(180);
      break;

    case 3:
      JoystickBLE.hat(-1);
      break;

    case 4:
      JoystickBLE.X(1023);
      break;

    case 5:
    default:
      JoystickBLE.X(512);
      break;
  }
}

void setup() {
  // Use the exact Arduino-Pico JoystickBLE data path that already proved HID
  // traffic on this Pico 2 W generation. No OAG Bluetooth host stack is active
  // in this diagnostic.
  JoystickBLE.begin(
    "OAG PC BLE GAMEPAD",
    "OAG PC BLE GAMEPAD"
  );

  JoystickBLE.setBattery(100);
  setNeutral();

  nextStepMs = millis() + kStartupDelayMs;
}

void loop() {
  const uint32_t now = millis();

  if (static_cast<int32_t>(now - nextStepMs) < 0) {
    delay(1);
    return;
  }

  applyStep(stepIndex);

  stepIndex = static_cast<uint8_t>(
    (stepIndex + 1u) % 6u
  );

  nextStepMs = now + kStepIntervalMs;
}
