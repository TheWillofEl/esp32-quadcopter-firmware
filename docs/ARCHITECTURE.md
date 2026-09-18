# Architecture

How the pieces fit together, end to end. This is a design-level companion
to the code comments (which explain WHY each individual file works the
way it does) — this document explains how the files compose into the
full system.

## Block diagram (data flow)

```
                     +------------------+
                     |   MPU-6050 IMU   |
                     | (accel + gyro)   |
                     +--------+---------+
                              | I2C (interrupt-driven)
                              v
                     +------------------+
                     |   mpu6050.h      |  raw accel/gyro, scaled to g / deg-per-sec
                     +--------+---------+
                              |
                              v
                     +------------------+
                     | complementary_   |  roll_deg, pitch_deg
                     | filter.h         |  (gyro integration, accel-corrected)
                     +--------+---------+
                              |
              +---------------+---------------+
              |                               |
              v                               v
   +--------------------+          +----------------------+
   | flight_controller.h |<---------| control_link.h        |
   | (angle PID -> rate  | setpoint | (Wi-Fi UDP commands:   |
   |  PID -> mixer)      |          |  arm, throttle, roll,  |
   +----------+----------+          |  pitch)                |
              |                     +-----------+------------+
              | 4 motor throttles               ^
              v                                  | UDP (shared socket
   +--------------------+                        |  with telemetry.h)
   |     motors.h        |            +----------+-----------+
   | (LEDC PWM, arming,  |            |   ground station /   |
   |  failsafe, battery  |            |   pilot_control.py   |
   |  cutoff)            |            +-----------------------+
   +----------+----------+
              | PWM (20kHz, 10-bit)
              v
   +--------------------+
   | 4x MOSFET gate      |
   | drivers -> brushed  |
   | DC motors           |
   +--------------------+

   (also, independent of the above): serial_console.h reads USB serial
   input directly and can call the same motors.h / flight_controller.h
   methods as control_link.h — a second, always-available command path,
   used for bench testing and as an emergency disarm route.

   battery.h reads ADC_BAT every loop -> battery_volts, which feeds both
   motors.checkBatteryCutoff() and telemetry.h's outgoing packets.

   telemetry.h sends roll/pitch/battery/loop_hz out over the same UDP
   socket control_link.h listens on, ~20 times/second, to whichever
   address last sent the drone a packet (ground_station.py or
   pilot_control.py, whichever connected most recently).
```

## Loop structure (`drone_firmware.ino`)

One `loop()` iteration, every call, in this order:

1. **Read IMU if a new sample is ready** (interrupt-flagged, not polled —
   see `mpu6050.h`). If so, measure the actual elapsed time (`dt`) since
   the last read and update the complementary filter.
2. **Poll the serial console** — always available, regardless of Wi-Fi
   state.
3. **Poll the control link** — at most one Wi-Fi command packet per loop.
4. **Run the self-level loop**, if active and a fresh IMU sample was just
   read this iteration. Produces four motor throttle values via the PID
   cascade + mixer, and calls `motors.setThrottle()` for each.
5. **Read the battery voltage** (oversampled ADC average).
6. **Check the comms failsafe and battery cutoff**, unconditionally —
   these can force a disarm regardless of what happened in steps 2-4.
7. **Update the measured loop-rate counter.**
8. **Send a telemetry packet**, but only every 50ms (20Hz) — much less
   often than the loop itself runs, since Wi-Fi traffic doesn't need to
   match sensor-read speed.

Steps 1 and 4 are the only two gated on "did the IMU actually produce a
new sample this iteration" — everything else runs unconditionally every
loop, which is why loop rate (see `measured_loop_hz`) matters: it's the
rate at which safety checks and command polling happen, not just the
sensor rate.

## IMU axis signs are a named, isolated assumption — not derived from LiteWing

`mpu6050.h` defines six sign constants (`GYRO_X_SIGN`, `GYRO_Y_SIGN`,
`GYRO_Z_SIGN`, `ACCEL_X_SIGN`, `ACCEL_Y_SIGN`, `ACCEL_Z_SIGN`, all
default `+1`) at the single point where raw IMU register values become
physical units, specifically so a sign correction is ever only a one-line
change instead of hunting through the filter or mixer math.

This exists because of a direct comparison against LiteWing's own source
(Circuit-Digest/LiteWing, an ESP-IDF/Crazyflie-based firmware for a
similar ESP32-S3 board — read for architectural comparison only, nothing
copied or ported, per this project's licensing rules — see
[README Credits](../README.md#credits)). Their MPU-6050 driver
(`hal/src/sensors_mpu6050_hm5883L_ms5611.c`) conditionally negates gyro X
depending on which hardware target it's compiled for:

```c
#ifdef CONFIG_TARGET_ESPLANE_V1
    sensorData.gyro.x = (gyroRaw.x - gyroBias.x) * SENSORS_DEG_PER_LSB_CFG;
#else
    sensorData.gyro.x = -(gyroRaw.x - gyroBias.x) * SENSORS_DEG_PER_LSB_CFG;
#endif
```

That's evidence from a hardware-proven, shipped product that an IMU axis
sign mismatch is a real, common failure mode — not a hypothetical one —
and that it varies even across board revisions of the *same* product
line. It is NOT evidence of what this board's correct sign values are;
those depend on this board's specific MPU-6050 mounting orientation and
can only be determined by the hardware bench test in
`docs/SELF_LEVEL_BENCH_TEST.md` Step 1 (tilt the board by hand, props
off, watch which way the self-level correction goes). The six constants
all default to `+1` — an explicit guess, not a measurement — until that
test says otherwise.

The same reasoning was considered for `mixer.h`'s motor-mixing signs, and
deliberately NOT applied: LiteWing's power-distribution code uses a
different motor-numbering/frame convention than this board's confirmed
arm layout, so there was no way to map their formula onto this one
without over-interpreting an unclear correspondence. Guessing a change
there would have been worse than leaving the existing, layout-derived
mixing table alone.

## Why cascaded control instead of one PID per axis

See `pid.h`'s header comment for the detailed reasoning. Short version:
splitting "angle error -> desired rate" (outer loop) from "rate error ->
motor correction" (inner loop) lets the inner loop react to disturbances
using the fastest signal available (raw gyro rate) with no extra lag,
while the outer loop only has to solve the slower problem of holding an
attitude. This is standard structure in this lineage of flight
controller (Crazyflie/Betaflight-style), reimplemented from first
principles for this specific airframe rather than copied — the gain
values, mixing signs, and code are all specific to this board and have
not been tuned on real hardware.

## Why two command interfaces (serial + Wi-Fi) instead of one

`serial_console.h` (USB) and `control_link.h` (Wi-Fi UDP) both exist
simultaneously and both call the exact same `Motors`/`FlightController`
methods — neither is a "real" interface with the other as a fallback.
This is deliberate:
- During bench testing (no working Wi-Fi control app yet, or wanting a
  more deliberate/harder-to-fat-finger command path), the serial console
  is simpler and more predictable.
- Once flying under Wi-Fi control, the serial console becomes a backup
  disarm path that a Wi-Fi failure specifically cannot block — typing
  `disarm` over a wired USB connection doesn't depend on the radio link
  that might be the thing failing.
- Both interfaces feed the identical 500ms comms failsafe
  (`Motors::feedFailsafe()`), so neither one can accidentally starve the
  other of "the drone is still being actively commanded" signal.

## What's explicitly out of scope (and why)

- **Yaw hold** — no magnetometer on this board (MPU-6050 is 6-axis:
  accel + gyro only, no compass). Gyro-only yaw would drift with no way
  to correct it, so it's not attempted. See `complementary_filter.h`.
- **Altitude hold** — no barometer or rangefinder. The J6 SPI/I2C
  expansion header is present and unpopulated for a possible future
  sensor (see `docs/PINMAP.md`), but nothing uses it yet.
- **Position hold / GPS** — no positioning sensor at all on this board.
- **ESC/ motor commutation protocols** (DShot, OneShot, etc.) — not
  applicable. These motors are brushed, driven by a plain low-side
  MOSFET switch, not brushless-with-ESC. See `motors.h`'s header comment.

## Where a future sensor would plug in

`docs/PINMAP.md`'s J6 header exposes an unpopulated SPI bus
(GPIO35/36/37/42) and a second I2C bus (GPIO40/41, `SDA1`/`SCL1` in
`pins.h`) specifically for this reason — e.g. a barometer for altitude
hold, or an optical flow sensor for position hold, without needing a
board respin. Nothing in the current firmware reads from either bus.
