# drone-firmware

Firmware for a custom ESP32-S3-WROOM-1 quadcopter board I designed and
built in Altium — the PCB itself is the airframe (four arms with motor
pads, flight controller in the centre).

**Status: in progress.** This README tracks what's actually done vs. not;
see [Progress](#progress) below. Full write-up (block diagram, photos,
flight results) lands once more phases are complete.

## Hardware

- **MCU:** ESP32-S3-WROOM-1-N8 (8MB flash, no PSRAM)
- **IMU:** MPU-6050 (6-axis accel + gyro) over I2C
- **Motors:** 4x low-side N-channel MOSFET drivers, PWM from the MCU
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
  common/pins.h       - single source of truth for every GPIO assignment
  hello_board/         - Phase 1: LED blink + UART bring-up sketch
  drone_firmware/       - main flight firmware (WIP)
docs/
  PINMAP.md            - full pin map, verified against the Altium schematic
  TOOLCHAIN.md         - exact toolchain versions for reproducible builds
```

## Progress

| Phase | What | Status |
|---|---|---|
| 0 | Pin map verified against schematic | Done |
| 1 | Toolchain + LED/UART bring-up sketch | Code written, **not yet flash-tested** (no USB access at time of writing) |
| 2 | MPU-6050 driver, battery ADC, attitude estimate, Wi-Fi telemetry | Not started |
| 3 | Motor PWM + safety (arming, failsafe, battery cutoff) | Not started |
| 4 | PID control loop, motor mixing | Not started |
| 5 | Control link (UDP) | Not started |
| 6 | Full README, license, polish | Not started |

## Safety

This board drives Li-ion batteries and spinning propellers. Non-negotiable
rules followed throughout development:
- Props off for all bench work.
- Board clamped down during any motor test.
- Arming sequence + comms failsafe implemented **before** motors ever spin
  with props attached.

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
