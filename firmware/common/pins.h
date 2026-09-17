// Pin map for Ahmed El Shatoury's custom ESP32-S3-WROOM-1-N8 quadcopter board.
// Source of truth: docs/PINMAP.md, verified against the Altium project on 2026-09-17.
// Every sketch in this repo includes this file instead of hardcoding pin numbers,
// so there is exactly one place to fix if the board ever changes.

#pragma once

// --- Motors (PWM to gate-driver MOSFETs, low-side, +BATT on the drain) ---
// Arm layout confirmed by Ahmed, viewed from above:
//   Front-Left  = MOT_2 (CW)      Front-Right = MOT_3 (CCW)
//   Rear-Left   = MOT_1 (CCW)     Rear-Right  = MOT_4 (CW)
#define PIN_MOT_1 6   // Rear-Left,  CCW
#define PIN_MOT_2 4   // Front-Left, CW
#define PIN_MOT_3 3   // Front-Right, CCW
#define PIN_MOT_4 5   // Rear-Right, CW

// --- Indicator LEDs (user-controllable, active-high through series resistor) ---
#define PIN_LED_RED   8
#define PIN_LED_GREEN 9
#define PIN_LED_BLUE  7

// --- IMU: MPU-6050 on I2C0 ---
#define PIN_SDA      10
#define PIN_SCL      11
#define PIN_MPU_INT  12

// --- Battery sense ---
// 100k/100k divider off +BATT -> roughly VBAT/2 at the ADC pin.
// High-impedance divider: average many samples in firmware, see Phase 2 notes.
#define PIN_ADC_BAT  2

// --- Boot / reset (normally driven by the auto-reset circuit via USB DTR/RTS,
// but these are also broken out to SW2/SW3 physical buttons) ---
#define PIN_IO0  0   // Boot strap / boot button

// --- J5 expansion header (GPIO15-18 broken out, unused by core flight firmware) ---
#define PIN_IO15 15
#define PIN_IO16 16
#define PIN_IO17 17
#define PIN_IO18 18

// --- J6 SPI expansion header + second I2C bus (unpopulated on this board,
// reserved for a future sensor such as a barometer or optical flow) ---
#define PIN_SPI_MOSI 35
#define PIN_SPI_CLK  36
#define PIN_SPI_MISO 37
#define PIN_SPI_CS   42
#define PIN_SDA1     40
#define PIN_SCL1     41

// --- UART0 to CP2102N is handled by the Arduino core's default Serial object;
// RX/TX pins (GPIO44/43) do not need to be set explicitly on ESP32-S3.
