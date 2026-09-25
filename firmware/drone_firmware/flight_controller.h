// Flight controller: owns the cascaded PID loops (angle -> rate) and the
// motor mixer, and drives Motors from a roll/pitch attitude estimate.
//
// This is Phase 4. Scope, deliberately: self-level (roll/pitch hold) only.
// - No altitude hold (no barometer/rangefinder on this board — J6 is
//   unpopulated, see docs/PINMAP.md).
// - No yaw hold (no magnetometer — see complementary_filter.h). Yaw rate
//   control is wired into mixer.h's math but not driven from here yet;
//   it's a Phase 5 addition once there's a control-link source for a yaw
//   setpoint.
// - No position hold (no GPS/optical flow).
// This controller's only job is: given a desired roll/pitch angle (0,0 =
// level, for now — a real "stick input" setpoint is Phase 5) and the
// current roll/pitch estimate, compute four motor throttles that correct
// the error. That's the same core loop real flight controllers build
// everything else on top of.
//
// Gains below are chosen conservatively (soft response, small output
// limits) specifically so that a bug or bad gain produces a sluggish/weak
// reaction rather than a violent one. See docs/MOTOR_BENCH_TEST.md and
// the TODO at the bottom of this file for the bench-test/tuning sequence.

#pragma once

#include "pid.h"
#include "mixer.h"
#include "motors.h"
#include "complementary_filter.h"

class FlightController {
 public:
  FlightController()
      // Outer angle loop: error is in degrees, output is a desired
      // rotation rate in degrees/second. Small ki (attitude error from a
      // steady-state gyro/accel offset is more of a calibration problem
      // than something to integrate away aggressively) and a rate output
      // limit of 90 deg/s — bench-tester tilting the board by hand won't
      // demand more than that, and it keeps the cascade conservative
      // until tuned.
      : angle_roll_pid_(4.0f, 0.0f, 0.0f, 90.0f),
        angle_pitch_pid_(4.0f, 0.0f, 0.0f, 90.0f),
        // Inner rate loop: error is in degrees/second, output is a
        // normalized motor-throttle correction in [-limit, +limit]. The
        // output limit (0.25 = +-25% throttle swing) is deliberately
        // small relative to MAX_THROTTLE_DURING_DEVELOPMENT (35%) so
        // mixing can't force a motor to full/negative even at the
        // extreme of what this loop can command.
        rate_roll_pid_(0.010f, 0.0f, 0.0002f, 0.25f),
        rate_pitch_pid_(0.010f, 0.0f, 0.0002f, 0.25f) {}

  // enable=true starts self-level mode at the given base throttle
  // (0.0-1.0). enable=false stops it (does not by itself zero the
  // motors or disarm — caller's job, see serial_console.h's 'manual').
  // Resets both PID cascades on every mode transition so old integral/
  // derivative state from a previous run never leaks into a new one.
  void setSelfLevel(bool enable, float base_throttle = 0.0f) {
    self_level_active_ = enable;
    base_throttle_ = constrain(base_throttle, 0.0f, 1.0f);
    angle_roll_pid_.reset();
    angle_pitch_pid_.reset();
    rate_roll_pid_.reset();
    rate_pitch_pid_.reset();
  }

  bool selfLevelActive() const {
    return self_level_active_;
  }

  // Call every loop iteration when armed. Runs the cascade (angle error
  // -> rate setpoint -> rate error -> motor correction -> mix -> apply)
  // and calls motors.setThrottle() for all four motors. Does nothing if
  // self-level mode isn't active — drone_firmware.ino only calls this
  // when it is, but the guard is here too so this class can't drive
  // motors from stale state.
  //
  // target_roll_deg/target_pitch_deg: 0.0 for level bench testing.
  // Phase 5 will pass a real stick-derived setpoint here instead of a
  // hardcoded 0.
  void update(ComplementaryFilter &filter, float gyro_x_dps, float gyro_y_dps,
              float dt, Motors &motors,
              float target_roll_deg = 0.0f, float target_pitch_deg = 0.0f) {
    if (!self_level_active_) {
      return;
    }

    float roll_angle_error = target_roll_deg - filter.roll_deg;
    float pitch_angle_error = target_pitch_deg - filter.pitch_deg;

    float target_roll_rate = angle_roll_pid_.update(roll_angle_error, dt);
    float target_pitch_rate = angle_pitch_pid_.update(pitch_angle_error, dt);

    // Rate error uses the gyro directly (not a filtered value) — the
    // whole point of the inner loop is to react to the fastest, least-
    // delayed signal available, and raw gyro rate is exactly that (see
    // pid.h's header comment on why the loop is cascaded this way).
    float roll_rate_error = target_roll_rate - gyro_x_dps;
    float pitch_rate_error = target_pitch_rate - gyro_y_dps;

    float roll_correction = rate_roll_pid_.update(roll_rate_error, dt);
    float pitch_correction = rate_pitch_pid_.update(pitch_rate_error, dt);

    last_throttles_ = mixMotors(base_throttle_, roll_correction, pitch_correction);

    motors.setThrottle(0, last_throttles_.mot1);
    motors.setThrottle(1, last_throttles_.mot2);
    motors.setThrottle(2, last_throttles_.mot3);
    motors.setThrottle(3, last_throttles_.mot4);
  }

  // For serial_console.h's 'status' command — last commanded throttles,
  // purely for visibility during bench testing.
  const MotorThrottles &lastThrottles() const {
    return last_throttles_;
  }

 private:
  PID angle_roll_pid_;
  PID angle_pitch_pid_;
  PID rate_roll_pid_;
  PID rate_pitch_pid_;

  bool self_level_active_ = false;
  float base_throttle_ = 0.0f;
  MotorThrottles last_throttles_ = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Bench-test / tuning sequence (see docs/MOTOR_BENCH_TEST.md for the
// props-off procedure this builds on):
// 1. Bench test with props off, board clamped: 'level 0' (base throttle
//    0%, motors won't spin but roll/pitch/rate math runs) and watch
//    'status' throttles react as the board is tilted by hand — confirms
//    sign conventions (a rightward tilt should show LEFT motors wanting
//    to spin faster) before any motor actually spins under this loop.
// 2. Then 'level <small %>' with props still off, to see real PWM output
//    change on an oscilloscope/multimeter without any risk.
// 3. Only after both of those look correct: props on, board firmly
//    restrained (e.g. mounted on a test stand or held down), very low
//    base throttle, to observe actual physical response — this is the
//    step where PID gains actually get tuned, starting from the
//    conservative values above and increasing kp first, then kd, adding
//    ki only if there's a steady-state error that doesn't go away.
