// Battery voltage sensing via ADC_BAT (GPIO2), through a 100k/100k
// resistor divider off +BATT.
//
// Why this needs averaging: a 100k/100k divider is high-impedance, and
// the ESP32's internal ADC has a small sample-and-hold capacitor that
// doesn't have time to fully charge from a high-impedance source in one
// sample. The result is a genuinely noisy reading, not just normal ADC
// jitter. Oversampling (many fast reads, averaged) cancels most of that
// noise out because it's roughly random from sample to sample.

#pragma once

#include <Arduino.h>
#include "../common/pins.h"

class BatteryMonitor {
 public:
  void begin() {
    analogReadResolution(12);        // 0-4095 counts
    analogSetPinAttenuation(PIN_ADC_BAT, ADC_11db); // full 0-3.3V input range
  }

  // Returns the divider's midpoint voltage in volts, averaged over
  // OVERSAMPLE_COUNT reads to cancel high-impedance-source noise.
  float readDividerVolts() {
    uint32_t sum = 0;
    for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
      sum += analogRead(PIN_ADC_BAT);
    }
    float avg_counts = sum / (float)OVERSAMPLE_COUNT;
    return avg_counts * (ADC_REF_VOLTS / ADC_MAX_COUNTS);
  }

  // Actual battery voltage: undo the 100k/100k divide-by-2.
  // NOTE: DIVIDER_RATIO is nominal (exactly 2.0 for matched 100k/100k).
  // Real resistors have tolerance (commonly +-1%), so this reading is
  // approximate until calibrated against a multimeter on real hardware —
  // see calibrate() below.
  float readBatteryVolts() {
    return readDividerVolts() * divider_ratio_;
  }

  // One-time calibration: give it the true battery voltage as measured
  // by a multimeter at the same moment, and it derives the actual
  // divider ratio (compensating for resistor tolerance) instead of
  // trusting the nominal 2.0x math above. Call this once during bring-up
  // and hardcode the printed ratio into divider_ratio_'s default, or
  // store it in NVS if it needs to be field-adjustable.
  float calibrate(float true_battery_volts) {
    float measured = readDividerVolts();
    divider_ratio_ = true_battery_volts / measured;
    return divider_ratio_;
  }

  // 1S Li-ion/LiPo nominal range. Used by the Phase 3 failsafe to decide
  // when to cut motors — do not fly below MIN_SAFE_VOLTS.
  static constexpr float FULL_VOLTS = 4.2f;
  static constexpr float MIN_SAFE_VOLTS = 3.3f; // conservative cutoff, well above the 3.0V hard-damage line

 private:
  static const int OVERSAMPLE_COUNT = 64;
  static constexpr float ADC_REF_VOLTS = 3.3f;
  static constexpr float ADC_MAX_COUNTS = 4095.0f;
  float divider_ratio_ = 2.0f; // nominal for matched 100k/100k; refine with calibrate()
};
