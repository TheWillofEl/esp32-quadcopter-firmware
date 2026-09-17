# Toolchain versions

Recorded here so a clean clone builds the same way every time. If you
update any of these, update this file in the same commit.

| Tool | Version |
|---|---|
| arduino-cli | 1.5.1 |
| ESP32 board package (`esp32:esp32`) | 3.3.11, via Espressif board index `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` |
| Target board (FQBN) | `esp32:esp32:esp32s3` |

## One-time setup (new machine)

```sh
# 1. Install arduino-cli (or use the Arduino IDE GUI instead, both share
#    the same Arduino15 data directory)
# https://arduino.github.io/arduino-cli/latest/installation/

# 2. Point it at the Espressif board index
arduino-cli config init
arduino-cli config set board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# 3. Install the ESP32 core
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## Build

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/hello_board
```

## Flash

Find the port first (Windows: check Device Manager for the CP2102N's COM
port; the auto-reset circuit means no button-holding should be needed):

```sh
arduino-cli board list
arduino-cli upload -p COM<N> --fqbn esp32:esp32:esp32s3 firmware/hello_board
```

## Monitor

```sh
arduino-cli monitor -p COM<N> -c baudrate=115200
```
