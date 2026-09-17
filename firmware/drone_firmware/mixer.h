// Motor mixing: turns "base throttle + roll correction + pitch correction
// + yaw correction" into four individual motor throttles, for this
// board's confirmed X-quad arm layout (see docs/PINMAP.md):
//
//   Front-Left  = MOT_2 (CW)      Front-Right = MOT_3 (CCW)
//   Rear-Left   = MOT_1 (CCW)     Rear-Right  = MOT_4 (CW)
//
// Sign convention (right-hand rule, matching the complementary filter's
// roll/pitch and this board's gyro axes):
//   - Positive roll correction  = commanded roll RIGHT  -> speed up the
//     two LEFT motors, slow down the two RIGHT motors.
//   - Positive pitch correction = commanded pitch forward (nose down)
//     -> speed up the two REAR motors, slow down the two FRONT motors.
//   - Positive yaw correction   = commanded yaw clockwise (viewed from
//     above) -> speed up the two CCW motors (MOT_1, MOT_3), slow down the
//     two CW motors (MOT_2, MOT_4) — reaction-torque coupling: spinning
//     the CCW pair faster than the CW pair yaws the airframe CW to
//     conserve angular momentum.
//
// This is the standard X-quad mixing table; the specific per-motor CW/CCW
// assignment and matching signs come from this board's confirmed layout,
// not copied from any reference project.
//
// NOTE: yaw control is not wired up yet. Phase 4 only closes the roll/
// pitch loops (that's what the MPU-6050 alone can stabilize well — see
// complementary_filter.h's note on why yaw isn't estimated). The
// yaw_correction parameter exists now so the mixing math is complete and
// ready for whenever a yaw rate setpoint source exists (Phase 5's control
// link could send one, using raw gyro_z_dps directly since no drift
// correction is needed for a RATE, only for an absolute angle).

#pragma once

#include <Arduino.h>

struct MotorThrottles {
  float mot1;  // Rear-Left,  CCW
  float mot2;  // Front-Left, CW
  float mot3;  // Front-Right, CCW
  float mot4;  // Rear-Right, CW
};

// All correction inputs are in the same normalized unit (typically the
// output of a rate-loop PID, already clamped to a small range like
// [-0.3, 0.3]) so they add linearly onto base_throttle without needing
// per-axis scaling here — tune that scaling via the PID output_limit
// instead (see drone_firmware.ino's PID construction).
inline MotorThrottles mixMotors(float base_throttle, float roll_correction,
                                 float pitch_correction,
                                 float yaw_correction = 0.0f) {
  MotorThrottles t;
  // Rear-Left (MOT_1, CCW): left (+roll), rear (+pitch), CCW (+yaw)
  t.mot1 = base_throttle + roll_correction + pitch_correction + yaw_correction;
  // Front-Left (MOT_2, CW): left (+roll), front (-pitch), CW (-yaw)
  t.mot2 = base_throttle + roll_correction - pitch_correction - yaw_correction;
  // Front-Right (MOT_3, CCW): right (-roll), front (-pitch), CCW (+yaw)
  t.mot3 = base_throttle - roll_correction - pitch_correction + yaw_correction;
  // Rear-Right (MOT_4, CW): right (-roll), rear (+pitch), CW (-yaw)
  t.mot4 = base_throttle - roll_correction + pitch_correction - yaw_correction;

  // Clamp each motor to [0, 1] here too, in addition to whatever
  // Motors::setThrottle() clamps — mixing can push an individual motor's
  // value outside [0,1] even when base_throttle and each correction are
  // individually in range (e.g. base_throttle=0.9 with a positive roll
  // correction can exceed 1.0). Motors::setThrottle() already clamps to
  // MAX_THROTTLE_DURING_DEVELOPMENT, so this is a belt-and-suspenders
  // clamp to keep MotorThrottles itself always a valid [0,1] value,
  // independent of what Motors happens to do with it.
  t.mot1 = constrain(t.mot1, 0.0f, 1.0f);
  t.mot2 = constrain(t.mot2, 0.0f, 1.0f);
  t.mot3 = constrain(t.mot3, 0.0f, 1.0f);
  t.mot4 = constrain(t.mot4, 0.0f, 1.0f);
  return t;
}
