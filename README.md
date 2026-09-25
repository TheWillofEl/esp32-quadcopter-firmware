# drone-firmware

Firmware for a custom ESP32-S3-WROOM-1 quadcopter board I designed and
built in Altium — the PCB itself is the airframe (four arms with motor
pads, flight controller in the centre).

**Status: firmware feature-complete for this scope (Phases 0-6).**

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for a block diagram and
full data-flow walkthrough, and [Progress](#progress) below for
per-phase status.

## Hardware

- **MCU:** ESP32-S3-WROOM-1-N8 (8MB flash, no PSRAM)
- **IMU:** MPU-6050 (6-axis accel + gyro) over I2C
- **Motors:** 4x small brushed coreless DC motors, each driven by a
  single low-side N-channel MOSFET (direct PWM duty cycle, no ESC/
  commutation protocol — confirmed against the schematic and with Ahmed)
- **Battery:** 1S Li-ion/LiPo, ~1000mAh, charged via MCP73871 with power-path
- **USB:** CP2102N USB-UART bridge with auto-reset circuit (no manual
  button presses needed to flash)
- Full pin map: [docs/PINMAP.md](docs/PINMAP.md)

## Toolchain

- **Arduino** (ESP32 Arduino core), not raw ESP-IDF — chosen for
  readability and fast iteration while keeping native-code performance
  for the real-time control loop.
- ESP32 Arduino core: `esp32:esp32` via the Espressif board index
  (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)
- Board: `esp32:esp32:esp32s3`
- Built and flashed with [arduino-cli](https://arduino.github.io/arduino-cli/)
  (exact versions recorded in [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md))

## Repo layout

```
firmware/
  common/pins.h                    - single source of truth for every GPIO assignment
  hello_board/                     - Phase 1: LED blink + UART bring-up sketch
  drone_firmware/                  - main flight firmware (feature-complete for this scope)
    drone_firmware.ino             - setup()/loop(), wires the modules below together
    mpu6050.h                      - IMU driver: init, WHO_AM_I check, interrupt-driven reads
    battery.h                      - battery ADC with oversampling + calibration
    complementary_filter.h         - roll/pitch estimate from accel + gyro
    telemetry.h                    - Wi-Fi access point + UDP telemetry broadcast
    motors.h                       - PWM motor control, arming, failsafe, battery cutoff
    pid.h                          - generic PID controller, used for both angle and rate loops
    mixer.h                        - X-quad motor mixing (throttle + roll/pitch/yaw -> 4 motor values)
    flight_controller.h            - cascaded angle->rate self-level loop, wires PID + mixer + motors
    control_link.h                 - Wi-Fi UDP command receiver (arm/throttle/roll/pitch setpoint)
    serial_console.h               - USB-serial bench-test command interface (always available)
tools/
  ground_station.py                - live plotter for roll/pitch/battery telemetry
  pilot_control.py                 - keyboard-driven control-link sender for bench testing
  requirements.txt                 - Python deps for tools/ scripts
docs/
  ARCHITECTURE.md                  - block diagram, data flow, why the design is structured this way
  PINMAP.md                        - full pin map, verified against the Altium schematic
  TOOLCHAIN.md                     - exact toolchain versions for reproducible builds
  MOTOR_BENCH_TEST.md              - required procedure before ever spinning a motor
  SELF_LEVEL_BENCH_TEST.md         - required procedure before trusting the self-level loop
  CONTROL_LINK_BENCH_TEST.md       - required procedure before flying the Wi-Fi control link
```

## Telemetry / ground station

The drone runs its **own Wi-Fi access point** (not your home router) —
same approach LiteWing uses. Default SSID/password are in
`firmware/drone_firmware/telemetry.h` (`drone-firmware` / `flyme1234` —
change these before flying anywhere with other people's drones nearby).

1. Flash and power on the drone.
2. Connect your computer's Wi-Fi to the drone's SSID.
3. `pip install -r tools/requirements.txt`
4. `python tools/ground_station.py` — plots roll, pitch, and battery
   voltage live, ~20 times a second.
5. `python tools/pilot_control.py` — bench-test keyboard control sender:
   arm, adjust throttle, and nudge a roll/pitch setpoint over the same
   UDP link (see `firmware/drone_firmware/control_link.h`). Required
   procedure before ever running this with props on:
   [docs/CONTROL_LINK_BENCH_TEST.md](docs/CONTROL_LINK_BENCH_TEST.md).

Both directions (telemetry out, commands in) use one small from-scratch
UDP protocol (plain-text `key=value` pairs) that Ahmed designed for this
board — not cflib/CRTP — see [Credits](#credits) for why that distinction
is kept deliberately.

## Progress

| Phase | What | Status |
|---|---|---|
| 0 | Pin map verified against schematic | Done |
| 1 | Toolchain + LED/UART bring-up sketch | Done |
| 2 | MPU-6050 driver, battery ADC, attitude estimate, Wi-Fi telemetry | Done |
| 3 | Motor PWM + safety (arming, failsafe, battery cutoff) | Done — bench-test procedure: [docs/MOTOR_BENCH_TEST.md](docs/MOTOR_BENCH_TEST.md) |
| 4 | Self-level control loop (cascaded angle->rate PID, X-quad motor mixing) | Done — bench-test procedure: [docs/SELF_LEVEL_BENCH_TEST.md](docs/SELF_LEVEL_BENCH_TEST.md) |
| 5 | Wi-Fi control link (UDP arm/throttle/roll/pitch commands) | Done — bench-test procedure: [docs/CONTROL_LINK_BENCH_TEST.md](docs/CONTROL_LINK_BENCH_TEST.md) |
| 6 | Full README, architecture doc, license, polish | Done |

## Safety

This board drives Li-ion batteries and spinning propellers. Non-negotiable
rules followed throughout development:
- Props off for all bench work.
- Board clamped down during any motor test.
- Arming sequence + comms failsafe implemented **before** motors ever spin
  with props attached — see `firmware/drone_firmware/motors.h`:
  - Motors force to zero duty at boot, before anything else runs.
  - `setThrottle()` does nothing unless `arm()` was called explicitly.
  - A comms failsafe disarms automatically if no command arrives within
    500ms, over either interface — USB serial (`serial_console.h`) or the
    Wi-Fi control link (`control_link.h`). The serial console is kept
    available even after the Wi-Fi link exists specifically as an
    always-on emergency disarm path — see `drone_firmware.ino`'s header
    comment.
  - A battery-voltage cutoff disarms unconditionally below 3.3V (1S
    Li-ion safe minimum), independent of anything the control loop does.
  - Throttle is hard-clamped to 35% max while still in development
    (`Motors::MAX_THROTTLE_DURING_DEVELOPMENT`), regardless of what's
    commanded.
- Motor bench-test procedure (props off, board clamped, one motor at a
  time): [docs/MOTOR_BENCH_TEST.md](docs/MOTOR_BENCH_TEST.md)
- Self-level control loop (`flight_controller.h`) only activates via an
  explicit `level` serial command, and only affects roll/pitch — no
  yaw/altitude/position control exists yet. PID gains are conservative
  by design, so a bug shows up as sluggish response rather than violent
  overcorrection. Bench-test procedure (props off first, sign/magnitude
  checks by hand, only then props on and restrained):
  [docs/SELF_LEVEL_BENCH_TEST.md](docs/SELF_LEVEL_BENCH_TEST.md)
- Wi-Fi control link (`control_link.h`) is a plain-text UDP protocol
  secured only by the AP's WPA2 password (see `telemetry.h`) — change the
  default SSID/password before flying anywhere that matters. Every
  command packet feeds the same 500ms comms failsafe as the serial
  console; a pilot app that stops sending (crash, Wi-Fi drop, out of
  range) auto-disarms within 500ms, same mechanism as the serial test.
  Bench-test procedure: [docs/CONTROL_LINK_BENCH_TEST.md](docs/CONTROL_LINK_BENCH_TEST.md)

## Known limitations

**Deliberately out of scope for this project's current form**: yaw hold,
altitude hold, position hold — see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#whats-explicitly-out-of-scope-and-why)
for why, and where a future sensor addition would plug in if that ever
changes.

## Credits

This firmware is a from-scratch implementation for different hardware. No
code is copied from any of these projects — they were read for
understanding (control loop structure, sensor fusion approach, packet
format) and reimplemented independently for this board's different pinout,
charger, and UART bridge.

- [LiteWing](https://circuitdigest.com/wiki/litewing/) ([repo](https://github.com/Circuit-Digest/LiteWing))
  — the board that inspired this build's form factor. CC BY-NC-SA 4.0.
- [ESP-Drone](https://github.com/espressif/esp-drone) — Espressif's ESP32
  port of Crazyflie.
- [Crazyflie firmware](https://github.com/bitcraze/crazyflie-firmware)
  (Bitcraze) — origin of the flight control algorithms in this lineage. GPL.

## License

MIT — see [LICENSE](LICENSE). This repo contains only original code plus
permissively-licensed dependencies; nothing derived from the GPL/CC-licensed
projects above is included.
