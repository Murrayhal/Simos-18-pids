// Measures the ECU's PWM command on the actuator signal line.
//
// The flap actuator is a servo: the ECU sends a PWM signal whose duty cycle is
// the commanded position. We never have to guess what duty means "open" — we
// watch the ECU drive the car's own actuator, in each drive select mode, and
// record what it sends. `learn` then stores exactly those numbers, so an
// override replays a command the car already produces rather than an invented
// one.
//
// Fed edge timestamps in microseconds from an interrupt, so it holds no timers
// of its own and can be tested on a host.
#pragma once

#include "valve_types.h"

namespace valve {

class PwmMeter {
 public:
  // A measurement is considered settled once this many consecutive periods
  // agree to within the tolerances below.
  static const uint8_t kStableCount = 8;

  PwmMeter();

  void reset();

  // Call on every edge of the signal line. `level` is the level *after* the
  // edge, so a rising edge passes true.
  void onEdge(uint32_t nowUs, bool level);

  // Call periodically. Declares the signal dead if no edges have arrived,
  // which is how a static high or low line is distinguished from a live one.
  void tick(uint32_t nowUs);

  bool valid() const { return valid_; }
  // True when consecutive periods agree, i.e. the ECU is holding one command.
  bool stable() const { return valid_ && stableRuns_ >= kStableCount; }

  uint16_t frequencyHz() const { return frequencyHz_; }
  DutyTenths duty() const { return duty_; }

  // Edges seen since reset. Zero means nothing is driving the line at all.
  uint32_t edgeCount() const { return edgeCount_; }

  // Set when the line has edges but no two consecutive periods agree. Either
  // the ECU is sweeping the actuator, or this is not a fixed-frequency PWM
  // signal at all and the install needs a scope on it.
  bool unsettled() const { return valid_ && stableRuns_ < kStableCount; }

 private:
  void publish(uint32_t highUs, uint32_t periodUs);

  bool haveRise_;
  uint32_t lastRiseUs_;
  uint32_t lastFallUs_;
  uint32_t lastEdgeUs_;

  uint16_t frequencyHz_;
  DutyTenths duty_;
  uint16_t lastFrequencyHz_;
  DutyTenths lastDuty_;
  uint8_t stableRuns_;
  bool valid_;
  uint32_t edgeCount_;
};

}  // namespace valve
