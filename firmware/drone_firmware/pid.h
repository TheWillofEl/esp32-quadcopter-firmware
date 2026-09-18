// PID controller — one instance per axis (roll, pitch), used twice each:
// once for the outer angle loop, once for the inner rate loop. See
// drone_firmware.ino for how the two loops are cascaded.
//
// Why cascade two PID loops instead of one: a single "angle in, motor
// command out" PID has to fight the drone's own rotational inertia
// directly, which makes it slow to react to sudden disturbances (a gust,
// an uneven motor). Splitting it in two — an outer loop that turns
// "angle error" into a desired rotation RATE, and an inner loop that
// turns "rate error" into a motor correction — lets the inner loop react
// almost instantly to disturbances (gyro rate is available every loop,
// with no filtering lag beyond the complementary filter itself), while
// the outer loop only has to worry about the slower job of holding an
// attitude. This is the standard structure used by every flight
// controller in this lineage (Crazyflie, Betaflight, etc.) — reimplemented
// here from first principles, not copied, since the actual gain values
// and code are specific to this airframe and haven't been tuned yet.
//
// Units are intentionally kept generic (this class doesn't know if it's
// working in degrees, degrees/second, or anything else) so the same class
// serves both loops — the caller decides what "error" and "output" mean.

#pragma once

#include <Arduino.h>
#include <math.h>

class PID {
 public:
  PID(float kp, float ki, float kd, float output_limit)
      : kp_(kp), ki_(ki), kd_(kd), output_limit_(output_limit) {}

  // Call once before first use, and any time the drone is disarmed — see
  // the note on integral windup below for why disarm must reset this.
  void reset() {
    integral_ = 0.0f;
    prev_error_ = 0.0f;
    has_prev_error_ = false;
  }

  // error: setpoint - measurement, in whatever units this instance is
  // configured for (degrees for the angle loop, degrees/second for the
  // rate loop).
  // dt: seconds since the previous update() call — MEASURED, not assumed,
  // same rule as ComplementaryFilter::update(). A control loop with a
  // fixed-dt assumption silently misbehaves the moment the actual loop
  // rate drifts (e.g. from the battery-ADC cost flagged in
  // drone_firmware.ino); measuring dt keeps the physics correct even if
  // the loop period isn't perfectly constant.
  float update(float error, float dt) {
    if (dt <= 0.0f) {
      // A zero or negative dt (clock issue, first call) would make the
      // derivative term divide by ~0 and spike to a huge, wrong value.
      // Skip integrating/differentiating this call rather than risk a
      // motor command spike from a bad timestamp — return the last
      // output's proportional-only equivalent instead.
      return constrain(kp_ * error, -output_limit_, output_limit_);
    }

    integral_ += error * dt;
    // Clamp the integral term itself (not just the final output) to the
    // output limit's range divided by ki_. This is "integral clamping" —
    // it stops the integral term from winding up far past what the
    // output could ever use, which matters most right after the drone is
    // disarmed and sitting at a nonzero angle (e.g. tilted on the bench):
    // without this clamp, the integral would keep accumulating error the
    // whole time it's disarmed and then slam the output the instant it's
    // armed again. reset() (called on every disarm, see motors.h /
    // drone_firmware.ino) handles the disarmed case directly; this clamp
    // is the second line of defense for the still-armed case (e.g.
    // sitting against a hard mechanical limit).
    if (ki_ != 0.0f) {
      // fabsf(): a negative ki_ would otherwise make integral_limit
      // negative, and constrain(x, -negative, negative) passes a min
      // greater than max — undefined/wrong. No gain in this codebase is
      // currently negative (see flight_controller.h), but this class is
      // generic and shouldn't silently misbehave if one ever is.
      float integral_limit = fabsf(output_limit_ / ki_);
      integral_ = constrain(integral_, -integral_limit, integral_limit);
    }

    float derivative = 0.0f;
    if (has_prev_error_) {
      derivative = (error - prev_error_) / dt;
    }
    prev_error_ = error;
    has_prev_error_ = true;

    float output = kp_ * error + ki_ * integral_ + kd_ * derivative;
    return constrain(output, -output_limit_, output_limit_);
  }

 private:
  float kp_;
  float ki_;
  float kd_;
  float output_limit_;

  float integral_ = 0.0f;
  float prev_error_ = 0.0f;
  bool has_prev_error_ = false;
};
