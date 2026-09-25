// Minimal MPU-6050 driver: init, WHO_AM_I check, interrupt-driven reads.
//
// Why write our own instead of using a library: the goal of this project
// is understanding, and a 6-axis IMU over I2C is simple enough to talk to
// directly with a handful of register writes. No third-party dependency
// to license-check either.
//
// Datasheet register reference: MPU-6000/MPU-6050 Register Map and
// Descriptions, Rev 4.2 (public, Invensense/TDK).

#pragma once

#include <Wire.h>
#include "../common/pins.h"

// Axis sign configuration — see the comment block on
// MPU6050::readIfReady() below before touching these.
//
// Default is +1 (no flip) for every axis. Change a single constant to -1
// here if docs/SELF_LEVEL_BENCH_TEST.md Step 1 (tilt the board by hand,
// props off, watch which way the self-level correction goes) shows an
// axis is backwards — that's the intended way to determine these values
// for a specific board. Never change a value "because it looks more
// plausible"; only an observed direction mismatch justifies it.
#define GYRO_X_SIGN 1
#define GYRO_Y_SIGN 1
#define GYRO_Z_SIGN 1
#define ACCEL_X_SIGN 1
#define ACCEL_Y_SIGN 1
#define ACCEL_Z_SIGN 1

class MPU6050 {
 public:
  // Raw values converted to physical units.
  float accel_x_g, accel_y_g, accel_z_g;   // g (9.81 m/s^2)
  float gyro_x_dps, gyro_y_dps, gyro_z_dps; // degrees/second

  // volatile: written from the interrupt handler, read from the main loop.
  static volatile bool data_ready;

  bool begin() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000); // MPU-6050 supports fast mode I2C, 400kHz

    // WHO_AM_I (register 0x75) should read back the I2C address, 0x68,
    // confirming we're actually talking to an MPU-6050 and not silence
    // or a different chip on the bus.
    uint8_t who_am_i = readRegister(REG_WHO_AM_I);
    if (who_am_i != I2C_ADDRESS) {
      return false;
    }

    // Wake the chip up — it powers on in sleep mode.
    writeRegister(REG_PWR_MGMT_1, 0x00);
    delay(100);

    // +-500 deg/s gyro range (FS_SEL=1). Plenty of headroom for a
    // quadcopter's rate loop without wasting resolution.
    writeRegister(REG_GYRO_CONFIG, 0x08);

    // +-4g accel range (AFS_SEL=1). A quad rarely sees more than ~2g of
    // thrust-axis acceleration; 4g leaves margin for vibration/impacts.
    writeRegister(REG_ACCEL_CONFIG, 0x08);

    // Digital low-pass filter: DLPF_CFG=3 -> ~44Hz accel / ~42Hz gyro
    // bandwidth. Cuts motor vibration noise without adding much lag.
    writeRegister(REG_CONFIG, 0x03);

    // Enable the "data ready" interrupt on INT so we don't have to poll.
    writeRegister(REG_INT_ENABLE, 0x01);

    pinMode(PIN_MPU_INT, INPUT);

    return true;
  }

  // Call this once per loop iteration; it only does I2C work when the
  // interrupt has actually fired, so it's cheap to call unconditionally.
  //
  // IMU AXIS SIGN CONVENTION.
  // A raw gyro/accel reading's sign depends on the exact rotational
  // orientation of the MPU-6050 die inside its package as soldered onto
  // this board, relative to which direction this board calls "roll
  // right" / "pitch forward" — that's a physical fact about this specific
  // PCB, not something derivable from the datasheet or from another
  // project's code. Cross-referencing LiteWing's own MPU-6050 driver
  // (Circuit-Digest/LiteWing, hal/src/sensors_mpu6050_hm5883L_ms5611.c)
  // confirms this is a real, common consideration on hardware very close
  // to this one: their driver conditionally negates gyro X depending on
  // which board revision it's compiled for —
  //   #ifdef CONFIG_TARGET_ESPLANE_V1
  //     sensorData.gyro.x = (gyroRaw.x - gyroBias.x) * SENSORS_DEG_PER_LSB_CFG;
  //   #else
  //     sensorData.gyro.x = -(gyroRaw.x - gyroBias.x) * SENSORS_DEG_PER_LSB_CFG;
  //   #endif
  // — a shipped product needed a per-board sign flip. Their actual sign
  // value doesn't transfer to this board (different hardware, different
  // mounting), so it isn't reflected below — but the fact that it was
  // necessary at all is the reason GYRO_X_SIGN etc. exist as named,
  // single-point-of-change constants above instead of being buried as
  // implicit +1 multiplications, or omitted entirely. See
  // docs/SELF_LEVEL_BENCH_TEST.md Step 1 for the bench procedure that
  // determines the correct values for this board.
  bool readIfReady() {
    if (!data_ready) {
      return false;
    }
    data_ready = false;

    uint8_t buf[14];
    readRegisters(REG_ACCEL_XOUT_H, buf, 14);

    int16_t raw_accel_x = (buf[0] << 8) | buf[1];
    int16_t raw_accel_y = (buf[2] << 8) | buf[3];
    int16_t raw_accel_z = (buf[4] << 8) | buf[5];
    // buf[6],buf[7] = temperature, unused
    int16_t raw_gyro_x = (buf[8] << 8) | buf[9];
    int16_t raw_gyro_y = (buf[10] << 8) | buf[11];
    int16_t raw_gyro_z = (buf[12] << 8) | buf[13];

    // Sensitivity scale factors for the ranges configured in begin():
    // AFS_SEL=1 (+-4g)    -> 8192 LSB/g
    // FS_SEL=1  (+-500dps) -> 65.5 LSB/(deg/s)
    accel_x_g = ACCEL_X_SIGN * raw_accel_x / 8192.0f;
    accel_y_g = ACCEL_Y_SIGN * raw_accel_y / 8192.0f;
    accel_z_g = ACCEL_Z_SIGN * raw_accel_z / 8192.0f;
    gyro_x_dps = GYRO_X_SIGN * raw_gyro_x / 65.5f;
    gyro_y_dps = GYRO_Y_SIGN * raw_gyro_y / 65.5f;
    gyro_z_dps = GYRO_Z_SIGN * raw_gyro_z / 65.5f;

    return true;
  }

  // Call this from an ISR attached to PIN_MPU_INT (RISING edge).
  static void IRAM_ATTR onInterrupt() {
    data_ready = true;
  }

 private:
  static const uint8_t I2C_ADDRESS = 0x68; // AD0 pulled to GND via R15 (confirmed in Altium), so AD0=0
  static const uint8_t REG_WHO_AM_I = 0x75;
  static const uint8_t REG_PWR_MGMT_1 = 0x6B;
  static const uint8_t REG_GYRO_CONFIG = 0x1B;
  static const uint8_t REG_ACCEL_CONFIG = 0x1C;
  static const uint8_t REG_CONFIG = 0x1A;
  static const uint8_t REG_INT_ENABLE = 0x38;
  static const uint8_t REG_ACCEL_XOUT_H = 0x3B;

  void writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  uint8_t readRegister(uint8_t reg) {
    uint8_t value;
    readRegisters(reg, &value, 1);
    return value;
  }

  void readRegisters(uint8_t start_reg, uint8_t *buf, uint8_t count) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(start_reg);
    Wire.endTransmission(false); // repeated start, keep the bus
    Wire.requestFrom(I2C_ADDRESS, count);
    for (uint8_t i = 0; i < count && Wire.available(); i++) {
      buf[i] = Wire.read();
    }
  }
};

volatile bool MPU6050::data_ready = false;
