// Serial console: a tiny text-command interface over USB serial for
// bench-testing motors before the real Wi-Fi control link (Phase 5)
// exists.
//
// Why serial commands instead of anything automatic: the project's
// safety rules require an explicit, deliberate action before any motor
// spins — typing a command into a terminal is about as deliberate as it
// gets, and it means nothing spins just because the board powered on.
// This console is meant to be temporary/bench-only scaffolding; once
// Phase 5's real control link exists, arming/throttle commands come
// from Wi-Fi packets instead, gated the same way (through Motors::arm()
// and Motors::setThrottle()).
//
// Commands (type into the Arduino serial monitor, 115200 baud, newline
// terminated):
//   arm              - arms the motors (required before any spin command works)
//   disarm           - disarms immediately, motors to zero
//   mot <1-4> <0-100> - set one motor to a throttle percent (clamped to
//                       Motors::MAX_THROTTLE_DURING_DEVELOPMENT)
//   stop             - all motors to zero (does not disarm)
//   status           - print armed state, battery voltage, roll/pitch
//
// PROPS OFF. BOARD CLAMPED. Every time, no exceptions — see the
// project's safety rules in the README and HANDOFF docs.

#pragma once

#include <Arduino.h>
#include "motors.h"
#include "battery.h"
#include "complementary_filter.h"

class SerialConsole {
 public:
  void begin() {
    Serial.println();
    Serial.println("--- Serial console ready. Commands: arm, disarm, mot <1-4> <0-100>, stop, status ---");
    Serial.println("--- PROPS OFF. BOARD CLAMPED. Every time. ---");
  }

  // Call every loop iteration. Non-blocking: only acts when a full line
  // has arrived.
  void poll(Motors &motors, BatteryMonitor &battery, ComplementaryFilter &filter) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (line_length_ > 0) {
          line_buffer_[line_length_] = '\0';
          handleLine(line_buffer_, motors, battery, filter);
          line_length_ = 0;
        }
      } else if (line_length_ < (int)(sizeof(line_buffer_) - 1)) {
        line_buffer_[line_length_++] = c;
      }
      // Silently drop characters past the buffer limit rather than
      // overflow — a too-long line just gets truncated/ignored.
    }
  }

 private:
  char line_buffer_[64];
  int line_length_ = 0;

  void handleLine(char *line, Motors &motors, BatteryMonitor &battery,
                   ComplementaryFilter &filter) {
    char *cmd = strtok(line, " ");
    if (cmd == nullptr) {
      return;
    }

    if (strcmp(cmd, "arm") == 0) {
      motors.arm();
      motors.feedFailsafe();
      Serial.println("ARMED. Motors will respond to 'mot' commands.");
      Serial.println("Remember: feed the failsafe by sending another command within 500ms, or it re-disarms.");

    } else if (strcmp(cmd, "disarm") == 0) {
      motors.disarm();
      Serial.println("DISARMED. Motors forced to zero.");

    } else if (strcmp(cmd, "stop") == 0) {
      motors.stopAll();
      Serial.println("All motors set to zero (still armed if it was armed).");

    } else if (strcmp(cmd, "mot") == 0) {
      char *idx_str = strtok(nullptr, " ");
      char *pct_str = strtok(nullptr, " ");
      if (idx_str == nullptr || pct_str == nullptr) {
        Serial.println("Usage: mot <1-4> <0-100>");
        return;
      }
      int idx = atoi(idx_str);
      int pct = atoi(pct_str);
      if (idx < 1 || idx > 4) {
        Serial.println("Motor index must be 1-4.");
        return;
      }
      if (!motors.isArmed()) {
        Serial.println("Not armed. Send 'arm' first.");
        return;
      }
      float throttle = constrain(pct, 0, 100) / 100.0f;
      motors.feedFailsafe();
      motors.setThrottle(idx - 1, throttle);
      Serial.printf("MOT_%d set to %d%% (clamped to max %.0f%% during development)\n",
                     idx, pct, Motors::MAX_THROTTLE_DURING_DEVELOPMENT * 100.0f);

    } else if (strcmp(cmd, "status") == 0) {
      Serial.printf("armed=%s  battery=%.2fV  roll=%.2f  pitch=%.2f\n",
                     motors.isArmed() ? "YES" : "no",
                     battery.readBatteryVolts(), filter.roll_deg, filter.pitch_deg);

    } else {
      Serial.printf("Unknown command: %s\n", cmd);
    }
  }
};
