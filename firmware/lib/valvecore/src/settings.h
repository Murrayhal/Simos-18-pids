// Persisted, user-tunable configuration.
#pragma once

#include "valve_types.h"

namespace valve {

// Bumped whenever the layout of Settings changes so stored blobs from an older
// firmware are discarded rather than misread.
static const uint16_t kSettingsVersion = 5;

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

// The flap actuator is a self-contained servo motor on three wires: 12 V,
// ground, and one signal line whose PWM duty cycle commands the position. We
// do not invent those numbers - they are measured off the ECU's own line with
// `learn`, so what we replay is exactly what the car already sends.
struct ActuatorSettings {
  // Carrier frequency of the ECU's command signal, measured on the car.
  uint16_t pwmHz;
  // Duty the ECU sends for each end position, measured on the car.
  DutyTenths openDutyTenths;
  DutyTenths closedDutyTenths;
  // Which end positions have actually been captured. Kept separate from the
  // duty values because 0 % is a legitimate command, not a "not set" marker.
  bool openLearned;
  bool closedLearned;
  // True once both positions are known. Until then the controller refuses to
  // drive the actuator at all.
  bool commissioned;
};

struct Settings {
  uint16_t version;

  ActuatorSettings actuator;

  // False for an install with no CAN tap at all: button and LED only, driving
  // the flap open or shut on demand. SMART mode disappears (it has nothing to
  // decide from) and every interlock that reads the bus is skipped, which puts
  // the whole burden of not draining the battery on taking the 12 V feed from
  // a switched source. See docs/no-canbus.md.
  bool canFitted;

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
// Vacuously true on an install with no CAN tap.
bool signalsCommissioned(const Settings &s);

// True when this mode is reachable given the configuration. SMART needs the
// bus; the rest do not.
bool modeAvailable(const Settings &s, Mode m);

// Next mode in the button cycle, skipping any that are unavailable.
Mode nextMode(const Settings &s, Mode current);

}  // namespace valve
