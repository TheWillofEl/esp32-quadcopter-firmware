// Phase 1 bring-up sketch: "hello board"
//
// Purpose: prove the toolchain, the auto-reset circuit, and the pin map are
// all correct, before any sensor or motor code is trusted. Does three things:
//   1. Blinks the three indicator LEDs in sequence.
//   2. Prints a heartbeat message over UART (USB serial via the CP2102N).
//   3. If you can flash this with `idf.py`/Arduino upload WITHOUT holding
//      any buttons, the auto-reset circuit (U6.A/U6.B, DTR/RTS -> EN/IO0)
//      is proven working.
//
// No motors, no sensors here on purpose — keep this sketch minimal so a
// failure can only mean "toolchain/pin map/reset circuit," not "my sensor
// code has a bug."

#include "../common/pins.h"

const int LED_PINS[] = {PIN_LED_RED, PIN_LED_GREEN, PIN_LED_BLUE};
const char *LED_NAMES[] = {"RED", "GREEN", "BLUE"};
const int NUM_LEDS = 3;

void setup() {
  Serial.begin(115200);
  // Give the host's serial monitor a moment to attach after auto-reset.
  delay(500);

  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  Serial.println();
  Serial.println("=== drone-firmware: hello_board ===");
  Serial.println("If you can read this, UART + auto-reset are working.");
  Serial.printf("Chip: %s, %d MHz, %d cores\n", ESP.getChipModel(),
                ESP.getCpuFreqMHz(), ESP.getChipCores());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
}

void loop() {
  static int heartbeat = 0;

  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(LED_PINS[i], HIGH);
    Serial.printf("[%d] LED %s ON\n", heartbeat, LED_NAMES[i]);
    delay(300);
    digitalWrite(LED_PINS[i], LOW);
  }

  heartbeat++;
  delay(700);
}
