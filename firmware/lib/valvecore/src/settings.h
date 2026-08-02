// Persisted, user-tunable configuration.
#pragma once

#include "valve_types.h"

namespace valve {

// Bumped whenever the layout of Settings changes so stored blobs from an older
// firmware are discarded rather than misread.
static const uint16_t kSettingsVersion = 3;

struct SmartSettings {
  // Hysteresis pair: open above openRpm, close again below closeRpm.
  uint16_t openRpm;
  uint16_t closeRpm;
  // Hysteresis pair on accelerator pedal position, percent.
  uint8_t openPedalPct;
  uint8_t closePedalPct;
  // Hold the flap closed below this road speed regardless of the rules above,
  // so crawling out of an estate at 06:00 stays civil. 0 disables.
  uint16_t quietBelowKph;
  // Treat Audi drive select "dynamic" as a request to open.
  bool followDriveSelect;
  // Raw value of the drive select signal that means dynamic. Discovered on the
  // car, see docs/can-signals.md.
  uint8_t dynamicRawValue;
};

struct SafetySettings {
  // Coolant must be at or above this before any override is allowed.
  int16_t minCoolantC;
  // A signal older than this is treated as invalid.
  uint16_t signalTimeoutMs;
  // Refuse to override at all when the bus gives us nothing usable.
  bool requireCanForOverride;
  // Refuse to override unless the engine is actually turning.
  bool requireEngineRunning;
  // RPM above which the engine counts as running.
  uint16_t engineRunningRpm;
  // Stay stock for this long after the engine starts.
  uint16_t startupQuietMs;
  // Minimum time the flap must sit in a state before it may change again, so
  // the actuator does not hammer around a threshold.
  uint16_t minDwellMs;
  // Battery window. Ignored when batterySenseFitted is false.
  bool batterySenseFitted;
  uint16_t minBatteryMv;
  uint16_t maxBatteryMv;
};

struct Settings {
  uint16_t version;

  // True when energising the solenoid CLOSES the flap (quiet), false when
  // energising OPENS it. Determined on the car during commissioning; see
  // docs/commissioning.md. Getting this backwards inverts every mode, which is
  // why there is no factory default that pretends to know the answer.
  bool energizedClosesValve;

  // True once the installer has confirmed the polarity above. Until then the
  // controller refuses to drive the solenoid at all.
  bool polarityConfirmed;

  // Mode selected at power on.
  Mode defaultMode;

  SmartSettings smart;
  SafetySettings safety;
  SignalDef signals[kSignalCount];
};

// Conservative defaults: every CAN signal disabled, polarity unconfirmed, so a
// freshly flashed board behaves exactly like the factory car until somebody
// has actually commissioned it.
void loadDefaults(Settings &s);

// True when every signal the current configuration depends on is enabled.
bool signalsCommissioned(const Settings &s);

}  // namespace valve
