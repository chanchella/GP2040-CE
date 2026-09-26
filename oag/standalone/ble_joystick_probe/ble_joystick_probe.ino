#include <Arduino.h>
#include <JoystickBLE.h>

// Standalone BLE joystick probe for Raspberry Pi Pico 2 W.
// No UI5K runtime, no USB Host, no Bluetooth Host, no controller parsing.
//
// Purpose:
// Prove whether the same Pico 2 W + phone can exchange HID gamepad input
// using Arduino-Pico's known-good JoystickBLE stack in complete isolation.
//
// Expected phone name: OAG BLE PROBE
//
// Automatic sequence (repeats):
//   1) Button 1 press
//   2) Button 1 release
//   3) D-pad Down
//   4) D-pad neutral
//   5) Left stick X full-right
//   6) Left stick X center
//
// Open Android Settings or a gamepad tester. If HID is working, the phone
// should receive events without any physical controller connected to the Pico.

static constexpr uint32_t kStepIntervalMs = 1200;

uint32_t nextStepMs = 0;
uint8_t stepIndex = 0;

void applyStep(uint8_t step) {
  switch (step) {
    case 0:
      JoystickBLE.button(1, true);
      Serial.println("PROBE: Button 1 DOWN");
      break;

    case 1:
      JoystickBLE.button(1, false);
      Serial.println("PROBE: Button 1 UP");
      break;

    case 2:
      JoystickBLE.hat(180);
      Serial.println("PROBE: D-Pad DOWN");
      break;

    case 3:
      JoystickBLE.hat(-1);
      Serial.println("PROBE: D-Pad NEUTRAL");
      break;

    case 4:
      JoystickBLE.X(1023);
      Serial.println("PROBE: Left X FULL RIGHT");
      break;

    case 5:
    default:
      JoystickBLE.X(512);
      Serial.println("PROBE: Left X CENTER");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println("========================================");
  Serial.println("OAG STANDALONE BLE JOYSTICK PROBE");
  Serial.println("Arduino-Pico JoystickBLE");
  Serial.println("Target: Raspberry Pi Pico 2 W");
  Serial.println("========================================");

  JoystickBLE.begin("OAG BLE PROBE", "OAG BLE PROBE");
  JoystickBLE.setBattery(100);

  // Start from a clean neutral state.
  JoystickBLE.button(1, false);
  JoystickBLE.hat(-1);
  JoystickBLE.X(512);
  JoystickBLE.Y(512);
  JoystickBLE.Z(512);
  JoystickBLE.Zrotate(512);
  JoystickBLE.sliderLeft(0);
  JoystickBLE.sliderRight(0);

  nextStepMs = millis() + 2500;
}

void loop() {
  const uint32_t now = millis();

  if ((int32_t)(now - nextStepMs) < 0) {
    delay(1);
    return;
  }

  applyStep(stepIndex);
  stepIndex = (stepIndex + 1) % 6;
  nextStepMs = now + kStepIntervalMs;
}
