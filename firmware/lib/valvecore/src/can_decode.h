// Turning raw powertrain-bus frames into a VehicleState.
#pragma once

#include "settings.h"
#include "valve_types.h"

namespace valve {

struct CanFrame {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
};

// Pulls `bitLength` bits out of a frame using DBC bit numbering.
// Returns false when the field does not fit inside the frame's DLC.
bool extractBits(const uint8_t *data, uint8_t dlc, uint8_t startBit,
                 uint8_t bitLength, Endian endian, uint32_t &raw);

// Same, but also applies scale and offset from the signal definition.
bool decodeSignal(const SignalDef &def, const CanFrame &frame, float &value);

// Accumulates frames into a VehicleState and ages signals out when they stop
// arriving. Holds no timers of its own; every call is given the current
// millisecond count by the caller, which keeps it testable.
class CanDecoder {
 public:
  explicit CanDecoder(const Settings &settings);

  // Feed a frame. Returns true if it matched at least one configured signal.
  bool onFrame(const CanFrame &frame, uint32_t nowMs);

  // Expire stale signals. Call this every loop, not just on frame arrival.
  void tick(uint32_t nowMs);

  const VehicleState &state() const { return state_; }

  // Frames seen since boot, useful for "is the bus even alive" diagnostics.
  uint32_t frameCount() const { return frameCount_; }

  void reset();

 private:
  void applySignal(SignalId id, float value, uint32_t nowMs);

  const Settings &settings_;
  VehicleState state_;
  uint32_t lastSeenMs_[kSignalCount];
  bool seen_[kSignalCount];
  uint32_t frameCount_;
};

}  // namespace valve
