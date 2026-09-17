# Pin Map — ESP32-S3-WROOM-1-N8 Quadcopter Board

Verified directly from the Altium project (`get_component_pins` on U8) on
2026-09-17, cross-checked against the official Espressif ESP32-S3-WROOM-1
datasheet pin table. Module confirmed as **plain N8 (8MB flash, no PSRAM)**
— the N8R8 (Octal PSRAM) part reference that first appeared for U8 was a
mislabeled Ultra Librarian import and does not reflect the real board.

Module footprint pin numbers (1-41) are physical solder-pad positions on
the WROOM-1 castellated module, not GPIO numbers — the two are not in the
same order. This table gives both.

## Confirmed, in-use signals

| Footprint Pin | GPIO   | Net        | Function                                   |
|---------------|--------|------------|---------------------------------------------|
| 4             | GPIO4  | MOT_2      | Motor 2 PWM — **Front-Left, spins CW**      |
| 5             | GPIO5  | MOT_4      | Motor 4 PWM — **Rear-Right, spins CW**      |
| 6             | GPIO6  | MOT_1      | Motor 1 PWM — **Rear-Left, spins CCW**      |
| 15            | GPIO3  | MOT_3      | Motor 3 PWM — **Front-Right, spins CCW**    |
| 7             | GPIO7  | LED_BLUE   | Blue indicator LED                          |
| 12            | GPIO8  | LED_RED    | Red indicator LED                           |
| 17            | GPIO9  | LED_GREEN  | Green indicator LED                         |
| 18            | GPIO10 | SDA        | I2C0 data — MPU-6050                        |
| 19            | GPIO11 | SCL        | I2C0 clock — MPU-6050                       |
| 20            | GPIO12 | MPU_INT    | MPU-6050 interrupt line                     |
| 38            | GPIO2  | ADC_BAT    | Battery voltage sense (100k/100k divider)   |
| 36            | GPIO44 | RX (U0RXD) | UART RX from CP2102N (USB-serial)           |
| 37            | GPIO43 | TX (U0TXD) | UART TX to CP2102N (USB-serial)             |
| 27            | GPIO0  | IO0        | Boot mode strap / boot button (SW2)         |
| 3             | EN     | EN         | Reset / chip enable (SW3, auto-reset U6.A)  |
| 8             | GPIO15 | IO15       | J5 expansion header                          |
| 9             | GPIO16 | IO16       | J5 expansion header                          |
| 10            | GPIO17 | IO17       | J5 expansion header                          |
| 11            | GPIO18 | IO18       | J5 expansion header                          |
| 13            | GPIO19 | IO19       | J5 expansion header (also native USB D-)    |
| 28            | GPIO35 | MOSI       | J6 SPI expansion header                     |
| 29            | GPIO36 | CLK        | J6 SPI expansion header                     |
| 30            | GPIO37 | MISO       | J6 SPI expansion header                     |
| 33            | GPIO40 | SDA1       | I2C1 — second bus, J4/J6                    |
| 34            | GPIO41 | SCL1       | I2C1 — second bus, J4/J6                    |
| 35            | GPIO42 | CS         | J6 SPI chip-select                          |

## Confirmed unconnected (do not use — reserved/strapping/no net)

| Footprint Pin | GPIO   | Why left unconnected                        |
|---------------|--------|----------------------------------------------|
| 14            | GPIO20 | Native USB D+ — no net on this board         |
| 16            | GPIO46 | Strapping pin (ROM boot messages)            |
| 21            | GPIO13 | No net                                        |
| 22            | GPIO14 | No net                                        |
| 23            | GPIO21 | No net                                        |
| 24            | GPIO47 | No net (also SPICLK diff pair, special-use)  |
| 25            | GPIO48 | No net (also SPICLK diff pair, special-use)  |
| 26            | GPIO45 | Strapping pin (VDD_SPI voltage select)       |
| 31            | GPIO38 | No net                                        |
| 32            | GPIO39 | No net (JTAG MTCK)                            |
| 39            | GPIO1  | No net                                        |

## Motor / arm layout (confirmed by Ahmed)

X-quad configuration, viewed from above:

```
   Front-Left (MOT_2, CW)      Front-Right (MOT_3, CCW)
                \\              //
                 \\            //
                  [ESP32-S3 centre]
                 //            \\
                //              \\
   Rear-Left (MOT_1, CCW)       Rear-Right (MOT_4, CW)
```

Diagonal pairs spin the same direction (standard X-quad convention —
this cancels reaction torque in a hover): MOT_2 + MOT_4 = CW,
MOT_1 + MOT_3 = CCW.

## Battery

- 1S Li-ion/LiPo, ~1000 mAh (confirmed by Ahmed; exact connector/part TBD)
- Charged via MCP73871 (U3), PROG resistors set ~charge current
- Voltage sensed via `ADC_BAT` (GPIO2) through a 100k/100k divider off
  `+BATT` — expect roughly half of true battery voltage at the ADC pin.
  High-impedance divider (100k) — firmware must oversample/average; see
  Phase 2 notes.

## Open items / not yet verified on hardware

- USB-C / CP2102N serial enumeration: **not yet tested** (no USB access
  at time of writing). Auto-reset circuit (U6.A/U6.B via DTR/RTS) is
  designed per the standard ESP auto-program pattern but unconfirmed in
  practice.
- Battery connector type/part number.
- Whether R26/R27 divider has a filter/smoothing capacitor — none is shown
  on the schematic sheet reviewed; treat ADC_BAT as noisy until measured.
