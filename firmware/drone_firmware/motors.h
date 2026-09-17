// Motor control: 4x small brushed coreless DC motors, each driven by a
// single low-side N-channel MOSFET (T1-T4) with a flyback diode across
// the motor and a gate pulldown resistor (confirmed against the
// schematic — no ESC chip, no commutation protocol; this is a direct
// PWM-duty-cycle-controls-speed circuit, the same as a toy-class
// quadcopter like the reference LiteWing board).
//
// PWM frequency: 20kHz, chosen to sit above the range of human hearing
// (~20kHz is the usual upper edge) so the motors don't produce an
// audible whine at partial throttle — a low-frequency PWM (e.g. 1kHz)
// literally makes the motor coil hum at that frequency. 20kHz is also
// comfortably within what the ESP32-S3's LEDC PWM peripheral and these
// small brushed motors' inductance can handle cleanly.
//
// PWM resolution: 10-bit (0-1023 duty steps) — plenty of throttle
// resolution for a small brushed motor without needing a slower PWM
// frequency to get more steps (resolution and max frequency trade off
// against each other in LEDC).
//
// SAFETY — this file implements the mechanisms; drone_firmware.ino is
// responsible for actually calling arm()/disarm() and checking
// checkFailsafe()/checkBatteryCutoff() every loop. See the class
// comments below and docs/PINMAP.md / docs/MOTOR_BENCH_TEST.md for the
// non-negotiable safety rules: props off, board clamped, arming
// sequence before any spin, failsafe before flight.

#pragma once

#include <Arduino.h>
#include "../common/pins.h"

class Motors {
 public:
  // Absolute ceiling on commanded throttle while this project is still
  // in bring-up/testing. 0.0-1.0 scale. Deliberately low — raise this
  // only once individual motor tests (Phase 3 test procedure) and the
  // control loop (Phase 4) are both proven safe. This is a hard clamp
  // applied inside setThrottle(), not just a suggestion.
  static constexpr float MAX_THROTTLE_DURING_DEVELOPMENT = 0.35f;

  // If no arm()-refreshing command arrives within this many milliseconds
  // (e.g. Wi-Fi drops, ground station crashes), the failsafe fires and
  // every motor is forced to zero. This check happens in
  // checkFailsafe(), which the main loop must call every iteration.
  static constexpr uint32_t FAILSAFE_TIMEOUT_MILLIS = 500;

  void begin() {
    // ESP32 Arduino core 3.x LEDC API: ledcAttach(pin, freq, resolution)
    // both configures and attaches a channel to a pin in one call (the
    // core picks and manages the underlying channel internally — the
    // older two-step ledcSetup()/ledcAttachPin(pin, channel) API from
    // core 2.x is gone). Every later call (ledcWrite) addresses the
    // motor by pin, not by a channel number.
    ledcAttach(PIN_MOT_1, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(PIN_MOT_2, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(PIN_MOT_3, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcAttach(PIN_MOT_4, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);

    // Motors must never spin at boot — this is the very first
    // requirement in the project's safety rules. Force zero duty on
    // all four channels immediately, before anything else can run.
    stopAll();
  }

  // --- Arming ---
  //
  // "Armed" means the flight code is allowed to command nonzero
  // throttle at all. Starting disarmed and requiring an explicit,
  // deliberate arm() call (which Phase 5's control link will gate
  // behind an actual pilot action, not just "the firmware booted") is
  // the whole point of an arming sequence: it makes "motors spin
  // unexpectedly at power-on" structurally impossible, not just
  // unlikely.
  void arm() {
    armed_ = true;
    last_command_millis_ = millis();
  }

  void disarm() {
    armed_ = false;
    stopAll();
  }

  bool isArmed() const {
    return armed_;
  }

  // Call this every time a valid command arrives from the control link
  // (Phase 5) to reset the failsafe timer. Until Phase 5 exists, nothing
  // calls this, which means checkFailsafe() will trip almost
  // immediately after arm() — that's the correct, safe behavior for an
  // unfinished control link: motors default to off.
  void feedFailsafe() {
    last_command_millis_ = millis();
  }

  // Call every loop iteration. Returns true if the failsafe just fired
  // this call (useful for logging/telemetry), false otherwise. Forces
  // disarm() when it fires — the caller doesn't need to do anything
  // else.
  bool checkFailsafe() {
    if (!armed_) {
      return false;
    }
    if (millis() - last_command_millis_ > FAILSAFE_TIMEOUT_MILLIS) {
      disarm();
      return true;
    }
    return false;
  }

  // --- Battery cutoff ---
  //
  // Call every loop with the latest battery voltage (from battery.h).
  // Returns true if the cutoff just fired this call. Forces disarm()
  // when it fires. This exists so a firmware bug in the control loop
  // cannot out-stubborn a dying battery — the cutoff is unconditional
  // and does not go through setThrottle() at all, it calls stopAll()
  // directly.
  bool checkBatteryCutoff(float battery_volts) {
    if (battery_volts > 0.0f && battery_volts < MIN_SAFE_BATTERY_VOLTS) {
      disarm();
      return true;
    }
    return false;
  }

  // --- Throttle ---
  //
  // motor_index: 0-3, matching MOT_1..MOT_4 (see pins.h / PINMAP.md for
  // which physical arm each corresponds to).
  // throttle: 0.0 (off) to 1.0 (max) — clamped internally to both
  // [0,1] AND MAX_THROTTLE_DURING_DEVELOPMENT.
  //
  // Does nothing (forces zero) if not armed — this is the second half
  // of the arming guarantee: even if calling code has a bug and calls
  // setThrottle() before arm(), the motor will not spin.
  void setThrottle(int motor_index, float throttle) {
    if (!armed_) {
      throttle = 0.0f;
    }
    throttle = constrain(throttle, 0.0f, MAX_THROTTLE_DURING_DEVELOPMENT);

    uint32_t duty = (uint32_t)(throttle * PWM_MAX_DUTY);
    ledcWrite(pinForMotor(motor_index), duty);
  }

  void stopAll() {
    ledcWrite(PIN_MOT_1, 0);
    ledcWrite(PIN_MOT_2, 0);
    ledcWrite(PIN_MOT_3, 0);
    ledcWrite(PIN_MOT_4, 0);
  }

 private:
  static const int PWM_FREQ_HZ = 20000;
  static const int PWM_RESOLUTION_BITS = 10;
  static const uint32_t PWM_MAX_DUTY = (1 << PWM_RESOLUTION_BITS) - 1; // 1023

  // Matches BatteryMonitor::MIN_SAFE_VOLTS in battery.h — duplicated as
  // a plain constant here (rather than #include-ing battery.h) to keep
  // this file's only dependency on hardware pins, not on another
  // module's class; drone_firmware.ino is responsible for keeping the
  // two in sync if either changes, and a compile-time check isn't worth
  // the coupling for a single float constant. If these ever diverge,
  // that's a bug — they represent the same physical fact (1S Li-ion
  // safe cutoff voltage).
  static constexpr float MIN_SAFE_BATTERY_VOLTS = 3.3f;

  bool armed_ = false;
  uint32_t last_command_millis_ = 0;

  int pinForMotor(int motor_index) {
    switch (motor_index) {
      case 0: return PIN_MOT_1;
      case 1: return PIN_MOT_2;
      case 2: return PIN_MOT_3;
      case 3: return PIN_MOT_4;
      default: return PIN_MOT_1; // shouldn't happen; fail toward a defined pin, not undefined behavior
    }
  }
};
