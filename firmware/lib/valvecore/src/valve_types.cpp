#include "valve_types.h"

#include <string.h>

namespace valve {

const char *modeName(Mode m) {
  switch (m) {
    case Mode::Auto: return "AUTO";
    case Mode::Smart: return "SMART";
    case Mode::Open: return "OPEN";
    case Mode::Quiet: return "QUIET";
    default: return "?";
  }
}

const char *targetName(Target t) {
  switch (t) {
    case Target::Stock: return "STOCK";
    case Target::Open: return "OPEN";
    case Target::Closed: return "CLOSED";
    default: return "?";
  }
}

const char *lockoutName(Lockout l) {
  switch (l) {
    case Lockout::None: return "none";
    case Lockout::NotCommissioned: return "not-commissioned";
    case Lockout::EngineOff: return "engine-off";
    case Lockout::Warmup: return "warmup";
    case Lockout::StartupDelay: return "startup-delay";
    case Lockout::CanLost: return "can-lost";
    case Lockout::Voltage: return "voltage";
    default: return "?";
  }
}

const char *signalName(SignalId s) {
  switch (s) {
    case SignalId::Rpm: return "rpm";
    case SignalId::Pedal: return "pedal";
    case SignalId::Speed: return "speed";
    case SignalId::Coolant: return "coolant";
    case SignalId::DriveSelect: return "drive";
    default: return "?";
  }
}

SignalId signalFromName(const char *name) {
  if (name == nullptr) return SignalId::Count;
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    SignalId id = static_cast<SignalId>(i);
    if (strcmp(name, signalName(id)) == 0) return id;
  }
  return SignalId::Count;
}

}  // namespace valve
