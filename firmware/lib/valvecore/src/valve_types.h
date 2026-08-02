// Shared types for the S3 8V exhaust valve controller core.
//
// Everything in valvecore is plain C++ with no Arduino dependency so it can be
// compiled and unit tested on a host machine.
#pragma once

#include <stdint.h>

namespace valve {

// User-selectable operating modes, cycled by the dashboard button.
enum class Mode : uint8_t {
  Auto = 0,   // pure factory behaviour, controller electrically out of the way
  Smart,      // controller decides from RPM / pedal / drive select
  Open,       // force the flap open (loud) whenever it is safe to do so
  Quiet,      // force the flap closed (quiet) whenever it is safe to do so
  Count
};

// What we want the exhaust flap to physically do.
enum class Target : uint8_t {
  Stock = 0,  // hand control back to the ECU
  Open,       // flap open, free flowing, loud
  Closed      // flap closed, gas routed through the muffler, quiet
};

// Why an override was refused. Surfaced on the LED and the serial console so a
// "my button does nothing" complaint is diagnosable without a laptop.
enum class Lockout : uint8_t {
  None = 0,
  NotCommissioned,  // solenoid polarity has never been confirmed on this car
  Voltage,          // battery outside the safe window
  CanLost,          // no usable bus data and the config demands it
  EngineOff,        // engine not turning, do not sit energised on a parked car
  Warmup,           // coolant below threshold
  StartupDelay      // inside the quiet window after engine start
};

const char *modeName(Mode m);
const char *targetName(Target t);
const char *lockoutName(Lockout l);

// Byte order of a signal packed into a CAN frame.
enum class Endian : uint8_t { Little = 0, Big = 1 };

// One decoded quantity we care about.
enum class SignalId : uint8_t {
  Rpm = 0,
  Pedal,
  Speed,
  Coolant,
  DriveSelect,
  Count
};

static const uint8_t kSignalCount = static_cast<uint8_t>(SignalId::Count);

const char *signalName(SignalId s);
// Returns SignalId::Count when the name is not recognised.
SignalId signalFromName(const char *name);

// Placement of a signal inside a frame. Bit numbering matches the usual DBC
// convention: bit 0 is the least significant bit of byte 0, bit 7 the most
// significant bit of byte 0, bit 8 the least significant bit of byte 1, and so
// on. For Endian::Big the start bit is the most significant bit of the value.
struct SignalDef {
  uint32_t canId;
  uint8_t startBit;
  uint8_t bitLength;
  Endian endian;
  float scale;
  float offset;
  bool enabled;
};

// Live picture of the car, rebuilt from the bus. Each field carries its own
// validity flag because signals arrive on different frames at different rates
// and any one of them can go stale on its own.
struct VehicleState {
  bool rpmValid = false;
  uint16_t rpm = 0;

  bool pedalValid = false;
  uint8_t pedalPct = 0;

  bool speedValid = false;
  uint16_t speedKph = 0;

  bool coolantValid = false;
  int16_t coolantC = 0;

  bool driveSelectValid = false;
  uint8_t driveSelectRaw = 0;

  // Millisecond timestamp of the last frame that decoded into any signal.
  uint32_t lastUpdateMs = 0;
  // True when at least one signal is currently valid.
  bool anyValid = false;
};

}  // namespace valve
