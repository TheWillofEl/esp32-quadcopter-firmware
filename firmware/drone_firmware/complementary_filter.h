// Complementary filter: combines the accelerometer and gyroscope into a
// single roll/pitch estimate that's more accurate than either sensor
// alone.
//
// Why you need both: the gyro measures rotation RATE (degrees/second).
// Integrating it over time gives you an angle, but tiny measurement
// errors accumulate every step, so a gyro-only angle estimate drifts
// away from the truth over seconds. The accelerometer measures
// direction relative to gravity, so it gives an absolute angle with no
// drift — but it's noisy and gets corrupted by any real acceleration
// (motor vibration, the drone actually moving), not just gravity.
//
// The complementary filter's trick: trust the gyro for fast changes
// (it has no lag) and trust the accelerometer to slowly correct the
// gyro's drift over time (it has no drift). It's a weighted blend:
//
//   angle = ALPHA * (angle_prev + gyro_rate * dt) + (1 - ALPHA) * accel_angle
//
// ALPHA close to 1 means "trust the gyro almost entirely, but let the
// accelerometer pull it back into line very slowly." That's the right
// choice for a quadcopter: motor vibration makes the accelerometer noisy
// enough that it should only ever be a slow correction, not the primary
// signal.
//
// Yaw is NOT estimated here — a single IMU (no magnetometer) cannot
// measure absolute heading; only gyro-integrated relative yaw is
// possible, and that will drift with no way to correct it. This board
// has no magnetometer, so yaw-hold is out of scope until/unless one is
// added on the J6/second-I2C expansion headers.

#pragma once

#include <math.h>

class ComplementaryFilter {
 public:
  float roll_deg = 0.0f;
  float pitch_deg = 0.0f;

  // alpha: weight given to the gyro-integrated angle each step (0-1).
  // 0.98 is a common, readable starting point: mostly gyro, slowly
  // corrected by the accelerometer. Tune during Phase 4 bench testing.
  explicit ComplementaryFilter(float alpha = 0.98f) : alpha_(alpha) {}

  // Call once, before the first update(), with a resting reading so the
  // filter starts at the true angle instead of ramping up from zero.
  void init(float accel_x_g, float accel_y_g, float accel_z_g) {
    roll_deg = accelRollDeg(accel_y_g, accel_z_g);
    pitch_deg = accelPitchDeg(accel_x_g, accel_y_g, accel_z_g);
  }

  // gyro_*_dps: degrees/second from the MPU-6050, already sign-correct
  // for this board's mounting orientation.
  // dt: seconds since the previous update() call — MEASURE this, don't
  // assume a fixed loop period (see README's control-loop timing note).
  void update(float gyro_x_dps, float gyro_y_dps,
              float accel_x_g, float accel_y_g, float accel_z_g,
              float dt) {
    float gyro_roll = roll_deg + gyro_x_dps * dt;
    float gyro_pitch = pitch_deg + gyro_y_dps * dt;

    float accel_roll = accelRollDeg(accel_y_g, accel_z_g);
    float accel_pitch = accelPitchDeg(accel_x_g, accel_y_g, accel_z_g);

    roll_deg = alpha_ * gyro_roll + (1.0f - alpha_) * accel_roll;
    pitch_deg = alpha_ * gyro_pitch + (1.0f - alpha_) * accel_pitch;
  }

 private:
  float alpha_;

  // Roll/pitch purely from the accelerometer's gravity vector direction.
  // Only valid at rest or near-hover, since it can't distinguish gravity
  // from real acceleration — that's exactly why it's blended, not used
  // alone.
  static float accelRollDeg(float ay, float az) {
    return atan2f(ay, az) * 180.0f / (float)M_PI;
  }

  static float accelPitchDeg(float ax, float ay, float az) {
    return atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
  }
};
