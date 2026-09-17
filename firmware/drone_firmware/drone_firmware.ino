// Main flight firmware.
//
// Phase 2 (current): sensors + telemetry only. No motor code runs yet —
// see README.md "Progress" table and docs/PINMAP.md for what's verified.
// Reads the MPU-6050, estimates roll/pitch with a complementary filter,
// reads the battery voltage, and streams both over the drone's own Wi-Fi
// access point via UDP for a ground-station script to plot live.
//
// Phase 3+ (motors, control loop, failsafes) is NOT in this file yet —
// deliberately, per the project's phase order: sensors and telemetry are
// proven working before any motor code exists, since a motor bug is a
// spinning-propeller safety issue and a sensor bug is not.

#include "../common/pins.h"
#include "mpu6050.h"
#include "battery.h"
#include "complementary_filter.h"
#include "telemetry.h"

MPU6050 imu;
BatteryMonitor battery;
ComplementaryFilter filter;
Telemetry telemetry;

uint32_t last_update_micros = 0;

// Loop-rate measurement: the original handoff plan explicitly calls for
// MEASURING the actual loop period rather than assuming a fixed rate
// once the PID control loop exists (Phase 4). Measuring it here too,
// even though nothing depends on it yet, so there's a running baseline
// for "how fast does sensors-only firmware run" before motor/PID load
// is added on top.
uint32_t loop_count = 0;
uint32_t hz_window_start_millis = 0;
uint32_t measured_loop_hz = 0;

const uint32_t TELEMETRY_INTERVAL_MILLIS = 50; // 20Hz — plenty for live plotting, keeps Wi-Fi traffic light
uint32_t last_telemetry_millis = 0;

void onMpuInterrupt() {
  MPU6050::onInterrupt();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== drone-firmware: Phase 2 (sensors + telemetry) ===");

  if (!imu.begin()) {
    // WHO_AM_I mismatch means either a wiring fault or the chip isn't
    // responding — halt loudly rather than fly blind on fake data.
    Serial.println("FATAL: MPU-6050 WHO_AM_I check failed. Halting.");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("MPU-6050 initialized (WHO_AM_I OK).");

  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), onMpuInterrupt, RISING);

  battery.begin();

  // Prime the filter with a resting reading so it starts at the true
  // angle instead of ramping up from zero over the first few seconds.
  while (!imu.readIfReady()) {
    delay(1);
  }
  filter.init(imu.accel_x_g, imu.accel_y_g, imu.accel_z_g);

  telemetry.begin();

  last_update_micros = micros();
  hz_window_start_millis = millis();

  Serial.println("Setup complete. Streaming telemetry over Wi-Fi AP.");
}

void loop() {
  if (imu.readIfReady()) {
    uint32_t now_micros = micros();
    float dt = (now_micros - last_update_micros) / 1000000.0f;
    last_update_micros = now_micros;

    filter.update(imu.gyro_x_dps, imu.gyro_y_dps,
                   imu.accel_x_g, imu.accel_y_g, imu.accel_z_g, dt);
  }

  loop_count++;
  uint32_t now_millis = millis();
  if (now_millis - hz_window_start_millis >= 1000) {
    measured_loop_hz = loop_count;
    loop_count = 0;
    hz_window_start_millis = now_millis;
  }

  if (now_millis - last_telemetry_millis >= TELEMETRY_INTERVAL_MILLIS) {
    last_telemetry_millis = now_millis;
    float battery_volts = battery.readBatteryVolts();
    telemetry.send(filter.roll_deg, filter.pitch_deg, battery_volts,
                    measured_loop_hz);
  }
}
