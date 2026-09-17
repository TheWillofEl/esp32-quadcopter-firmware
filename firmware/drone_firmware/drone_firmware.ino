// Main flight firmware.
//
// Phase 2: sensors + telemetry. Reads the MPU-6050, estimates roll/pitch
// with a complementary filter, reads battery voltage, streams both over
// the drone's own Wi-Fi access point via UDP.
//
// Phase 3: motor PWM + safety, bench-testable over USB serial (see
// serial_console.h) since the real Wi-Fi control link doesn't exist yet
// (that's Phase 5). Every loop iteration checks the comms failsafe and
// the battery cutoff, both of which force an immediate disarm — the
// arming sequence and both safety checks exist BEFORE any throttle
// command can ever reach a motor.
//
// Phase 4: self-level control loop (flight_controller.h) — cascaded
// angle->rate PID per roll/pitch axis, mixed to four motor throttles
// (mixer.h) for this board's confirmed X-quad layout. Bench test via the
// serial console's 'level <0-100>' command; see flight_controller.h's
// TODO for the exact hardware test sequence. Not flown — gains are
// conservative starting points, unverified on real hardware, and
// yaw/altitude/position are explicitly out of scope (see
// flight_controller.h header comment for why).
//
// Phase 5 (current): Wi-Fi control link (control_link.h). Reads pilot
// command packets (arm/throttle/roll/pitch setpoint) over the same UDP
// socket telemetry.h already uses, so a ground-station app can fly the
// self-level loop instead of driving it from the serial console. The
// serial console STAYS — it's still the recommended way to do the
// props-off bench tests from docs/MOTOR_BENCH_TEST.md and
// docs/SELF_LEVEL_BENCH_TEST.md, since typing into a terminal is a more
// deliberate, harder-to-fat-finger action than a UDP packet. Whichever
// interface last sent a command wins (both ultimately just call the same
// Motors/FlightController methods) — this is intentional: it means the
// serial console can always be used as an emergency disarm path even
// while flying under Wi-Fi control, since typing 'disarm' at the USB
// port is not something a wireless failure can block.
//
// PROPS OFF. BOARD CLAMPED DOWN. Every time, no exceptions, per the
// project's safety rules (README.md / HANDOFF docs).

#include "../common/pins.h"
#include "mpu6050.h"
#include "battery.h"
#include "complementary_filter.h"
#include "telemetry.h"
#include "motors.h"
#include "flight_controller.h"
#include "control_link.h"
#include "serial_console.h"

MPU6050 imu;
BatteryMonitor battery;
ComplementaryFilter filter;
Telemetry telemetry;
Motors motors;
FlightController flight;
ControlLink control_link;
SerialConsole console;

uint32_t last_update_micros = 0;
float last_dt_ = 0.0f;

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
  Serial.println("=== drone-firmware: Phase 5 (Wi-Fi control link) ===");

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

  // Motors: begin() forces all four to zero duty immediately — motors
  // must never spin at boot, per the project's safety rules.
  motors.begin();
  console.begin();

  last_update_micros = micros();
  hz_window_start_millis = millis();

  Serial.println("Setup complete. Streaming telemetry over Wi-Fi AP.");
}

void loop() {
  bool imu_updated = imu.readIfReady();
  if (imu_updated) {
    uint32_t now_micros = micros();
    float dt = (now_micros - last_update_micros) / 1000000.0f;
    last_update_micros = now_micros;
    last_dt_ = dt;

    filter.update(imu.gyro_x_dps, imu.gyro_y_dps,
                   imu.accel_x_g, imu.accel_y_g, imu.accel_z_g, dt);
  }

  // Bench-test command input over USB (Phase 3/4 interface, still the
  // recommended way to run the props-off procedures in docs/).
  console.poll(motors, battery, filter, flight);

  // Wi-Fi control link (Phase 5): reads at most one pilot command packet
  // per loop off the same UDP socket telemetry uses. Runs after the
  // serial console so either interface can issue a command this same
  // loop iteration — both ultimately just call Motors/FlightController
  // methods, so there's no real conflict, only ordinary last-write-wins,
  // which is fine since a human only ever drives one interface at a time
  // in practice.
  if (control_link.poll(telemetry.udp(), motors, flight)) {
    if (control_link.haveSenderIP()) {
      telemetry.setGroundStation(control_link.senderIP());
    }
  }

  // Self-level control loop: only actually drives motors when self-level
  // mode is on — turned on by either the serial console's 'level'
  // command or a control-link arm=1 packet (see flight_controller.h). No
  // effect on motors while in manual/idle mode. Runs every loop with a
  // fresh IMU reading, using the same measured dt as the complementary
  // filter update above, raw gyro rates straight from the IMU driver for
  // the inner rate loop (see flight_controller.h's comment on why the
  // rate loop uses raw gyro rather than the filtered angle), and the
  // latest roll/pitch setpoint from the control link — (0,0) / "hold
  // level" until a pilot app sends otherwise, which is also the only
  // case the serial console's bench test can exercise, since it has no
  // way to send a tilted setpoint.
  if (imu_updated) {
    flight.update(filter, imu.gyro_x_dps, imu.gyro_y_dps, last_dt_, motors,
                   control_link.targetRollDeg(), control_link.targetPitchDeg());
  }

  // Safety checks run every loop iteration, unconditionally — not on
  // the telemetry cadence — so a dead comms link or a dying battery is
  // caught as fast as the loop runs, not up to 50ms late.
  //
  // KNOWN COST, NOT YET MEASURED ON HARDWARE: readBatteryVolts() does 64
  // oversampled analogRead() calls every single loop iteration now. If
  // "status" or the printed loop_hz shows this is slowing the loop down
  // more than expected once flashed, the fix is to move this behind its
  // own short interval (e.g. every 10-20ms is still far faster than a
  // battery can actually drop dangerously) rather than every iteration
  // — don't lower OVERSAMPLE_COUNT in battery.h to compensate, since
  // that's what's actually taming the noisy 100k/100k divider.
  float battery_volts = battery.readBatteryVolts();
  if (motors.checkFailsafe()) {
    flight.setSelfLevel(false);
    Serial.println("FAILSAFE: no command received in time. Motors disarmed.");
  }
  if (motors.checkBatteryCutoff(battery_volts)) {
    flight.setSelfLevel(false);
    Serial.println("BATTERY CUTOFF: voltage below safe minimum. Motors disarmed.");
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
    telemetry.send(filter.roll_deg, filter.pitch_deg, battery_volts,
                    measured_loop_hz);
  }
}
