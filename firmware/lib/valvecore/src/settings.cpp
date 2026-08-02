#include "settings.h"

namespace valve {

void loadDefaults(Settings &s) {
  s.version = kSettingsVersion;

  // No assumptions about the car's actuator until somebody has measured it.
  // Zero duties are deliberately unusable, not a guess at a sane value.
  s.actuator.pwmHz = 0;
  s.actuator.openDutyTenths = 0;
  s.actuator.closedDutyTenths = 0;
  s.actuator.openLearned = false;
  s.actuator.closedLearned = false;
  s.actuator.commissioned = false;

  s.canFitted = true;
  s.defaultMode = Mode::Auto;

  s.smart.openRpm = 3200;
  s.smart.closeRpm = 2600;
  s.smart.openPedalPct = 60;
  s.smart.closePedalPct = 40;
  s.smart.quietBelowKph = 0;
  s.smart.followDriveSelect = false;
  s.smart.dynamicRawValue = 0;

  s.safety.minCoolantC = 60;
  s.safety.signalTimeoutMs = 500;
  s.safety.requireCanForOverride = true;
  s.safety.requireEngineRunning = true;
  s.safety.engineRunningRpm = 400;
  s.safety.startupQuietMs = 5000;
  s.safety.minDwellMs = 750;
  s.safety.batterySenseFitted = false;
  s.safety.minBatteryMv = 11000;
  s.safety.maxBatteryMv = 15500;

  // Placement of these signals on the powertrain bus varies between model
  // years, so nothing is enabled out of the box. Run `hunt` on the car and
  // commit the result with `sig`; see docs/can-signals.md.
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    s.signals[i].canId = 0;
    s.signals[i].startBit = 0;
    s.signals[i].bitLength = 16;
    s.signals[i].endian = Endian::Little;
    s.signals[i].scale = 1.0f;
    s.signals[i].offset = 0.0f;
    s.signals[i].enabled = false;
  }
  s.signals[static_cast<uint8_t>(SignalId::Pedal)].bitLength = 8;
  s.signals[static_cast<uint8_t>(SignalId::Coolant)].bitLength = 8;
  s.signals[static_cast<uint8_t>(SignalId::DriveSelect)].bitLength = 8;
}

bool signalsCommissioned(const Settings &s) {
  // Nothing to commission when there is no bus to read.
  if (!s.canFitted) return true;
  // RPM is the only signal the safety interlocks genuinely cannot do without.
  if (!s.signals[static_cast<uint8_t>(SignalId::Rpm)].enabled) return false;
  if (s.safety.minCoolantC > -273 &&
      !s.signals[static_cast<uint8_t>(SignalId::Coolant)].enabled) {
    return false;
  }
  if (s.smart.followDriveSelect &&
      !s.signals[static_cast<uint8_t>(SignalId::DriveSelect)].enabled) {
    return false;
  }
  if (s.smart.quietBelowKph > 0 &&
      !s.signals[static_cast<uint8_t>(SignalId::Speed)].enabled) {
    return false;
  }
  return true;
}

bool modeAvailable(const Settings &s, Mode m) {
  if (m == Mode::Smart && !s.canFitted) return false;
  return m < Mode::Count;
}

Mode nextMode(const Settings &s, Mode current) {
  for (uint8_t step = 1; step <= static_cast<uint8_t>(Mode::Count); ++step) {
    const Mode candidate = static_cast<Mode>(
        (static_cast<uint8_t>(current) + step) %
        static_cast<uint8_t>(Mode::Count));
    if (modeAvailable(s, candidate)) return candidate;
  }
  return Mode::Auto;
}

}  // namespace valve
