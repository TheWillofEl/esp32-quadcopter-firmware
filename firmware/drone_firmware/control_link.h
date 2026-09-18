// Control link: receives pilot commands over the same UDP socket
// telemetry.h already listens on, and turns them into arm/disarm/
// throttle/setpoint calls on Motors and FlightController — the Phase 5
// replacement for serial_console.h's bench-test commands.
//
// Wire format: plain-text, comma-separated `key=value` pairs, one UDP
// packet per command, matching the same style as telemetry.h's outgoing
// packets (deliberately — one human-readable protocol for both
// directions, not two). Example packet:
//
//   arm=1,thr=35,roll=0,pitch=0
//
// This is Ahmed's own from-scratch protocol, not cflib/CRTP — see
// README.md Credits for why that distinction is kept deliberately (CRTP
// is Bitcraze/Crazyflie's binary packet protocol, GPL-licensed lineage;
// this is a different, much simpler design built for this board).
//
// Fields (all optional per-packet; only present fields are applied):
//   arm=1|0     - 1 requests armed (only takes effect if not already
//                 failsafe/battery-cutoff disarmed), 0 requests disarm.
//   thr=0-100   - base throttle percent for the self-level loop (same
//                 role as the serial console's 'level <pct>').
//   roll=deg    - target roll angle in degrees (small range expected,
//                 e.g. -30..30 — a "stick tilt", not an aerobatic target).
//   pitch=deg   - target pitch angle in degrees, same range.
//
// Every packet — regardless of which fields it sets — counts as "a
// command arrived" and feeds the comms failsafe (Motors::feedFailsafe()),
// exactly like every serial console command did in Phase 3/4. A pilot
// application is expected to send packets at a steady rate (e.g. 20-50Hz,
// matching stick input polling) even when nothing has changed, precisely
// so the 500ms failsafe timeout has something to keep resetting — a
// control link that only sends on change would let the failsafe trip
// during a perfectly fine hover with no stick movement, which is wrong.

#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "motors.h"
#include "flight_controller.h"

class ControlLink {
 public:
  // Call every loop. Non-blocking — reads at most one waiting packet per
  // call (there's no reason to drain a backlog for a live control link;
  // an old queued command is stale and should be superseded by the next
  // fresh one, not applied late). Returns true if a command packet was
  // received and applied this call.
  bool poll(WiFiUDP &udp, Motors &motors, FlightController &flight) {
    int packet_size = udp.parsePacket();
    if (packet_size <= 0) {
      return false;
    }

    last_sender_ip_ = udp.remoteIP();
    have_sender_ip_ = true;

    char buf[96];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) {
      return false;
    }
    buf[len] = '\0';

    // Any parseable command packet, even one that changes nothing, feeds
    // the failsafe — see header comment on why.
    motors.feedFailsafe();

    bool want_arm = false;
    bool have_arm_field = false;
    float throttle_pct = last_throttle_pct_;
    float target_roll = 0.0f;
    float target_pitch = 0.0f;
    bool have_setpoint = false;

    char *saveptr = nullptr;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token != nullptr) {
      char *eq = strchr(token, '=');
      if (eq != nullptr) {
        *eq = '\0';
        const char *key = token;
        const char *value = eq + 1;
        if (strcmp(key, "arm") == 0) {
          want_arm = (atoi(value) != 0);
          have_arm_field = true;
        } else if (strcmp(key, "thr") == 0) {
          throttle_pct = constrain((float)atof(value), 0.0f, 100.0f);
        } else if (strcmp(key, "roll") == 0) {
          target_roll = (float)atof(value);
          have_setpoint = true;
        } else if (strcmp(key, "pitch") == 0) {
          target_pitch = (float)atof(value);
          have_setpoint = true;
        }
      }
      token = strtok_r(nullptr, ",", &saveptr);
    }

    if (have_arm_field) {
      if (want_arm) {
        motors.arm();
        flight.setSelfLevel(true, throttle_pct / 100.0f);
      } else {
        motors.disarm();
        flight.setSelfLevel(false);
        // Reset the remembered throttle/setpoint to zero on every disarm,
        // not just the motors. Without this, a later bare 'arm=1' packet
        // (no thr= field — the protocol allows omitting it, even though
        // tools/pilot_control.py always sends one) would resume self-level
        // at whatever throttle was last commanded before this disarm,
        // via the throttle_pct = last_throttle_pct_ seed above. That
        // defeats the entire point of requiring an explicit arm: a
        // fresh arm must never be able to start at a nonzero, stale
        // throttle just because the arming packet itself omitted thr=.
        throttle_pct = 0.0f;
        last_target_roll_ = 0.0f;
        last_target_pitch_ = 0.0f;
      }
    } else if (motors.isArmed()) {
      // Already armed and this packet updated throttle/setpoint without
      // re-sending arm=1 — keep self-level active, just refresh the base
      // throttle so a mid-flight throttle change takes effect without
      // needing to also resend the arm field every packet.
      flight.setSelfLevel(true, throttle_pct / 100.0f);
    }

    last_throttle_pct_ = throttle_pct;
    last_target_roll_ = have_setpoint ? target_roll : last_target_roll_;
    last_target_pitch_ = have_setpoint ? target_pitch : last_target_pitch_;

    return true;
  }

  float targetRollDeg() const { return last_target_roll_; }
  float targetPitchDeg() const { return last_target_pitch_; }

  // Exposed so drone_firmware.ino can hand the pilot's address to
  // Telemetry directly the moment a real command packet arrives, rather
  // than waiting for a separate "hello" packet that a control packet
  // itself already proves the existence of.
  bool haveSenderIP() const { return have_sender_ip_; }
  IPAddress senderIP() const { return last_sender_ip_; }

 private:
  float last_throttle_pct_ = 0.0f;
  float last_target_roll_ = 0.0f;
  float last_target_pitch_ = 0.0f;
  IPAddress last_sender_ip_;
  bool have_sender_ip_ = false;
};
