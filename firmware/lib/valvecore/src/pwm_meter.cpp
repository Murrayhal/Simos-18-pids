#include "pwm_meter.h"

namespace valve {
namespace {

// A period must be within this fraction of the previous one to count as the
// same signal. Generous, because we are measuring in an interrupt on a busy
// microcontroller in a car.
uint32_t absDiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

// No edges for this long means the line is parked high or low.
const uint32_t kDeadUs = 200000;  // 200 ms

}  // namespace

PwmMeter::PwmMeter() { reset(); }

void PwmMeter::reset() {
  haveRise_ = false;
  lastRiseUs_ = 0;
  lastFallUs_ = 0;
  lastEdgeUs_ = 0;
  frequencyHz_ = 0;
  duty_ = 0;
  lastFrequencyHz_ = 0;
  lastDuty_ = 0;
  stableRuns_ = 0;
  valid_ = false;
  edgeCount_ = 0;
}

void PwmMeter::publish(uint32_t highUs, uint32_t periodUs) {
  if (periodUs == 0) return;
  // Guard against nonsense from a missed edge before dividing.
  if (highUs > periodUs) return;
  if (periodUs < 20 || periodUs > 1000000) return;  // 1 Hz .. 50 kHz

  const uint32_t hz = 1000000u / periodUs;
  const uint32_t d =
      static_cast<uint32_t>((static_cast<uint64_t>(highUs) * kDutyMax) / periodUs);

  frequencyHz_ = static_cast<uint16_t>(hz > 65535u ? 65535u : hz);
  duty_ = static_cast<DutyTenths>(d > kDutyMax ? kDutyMax : d);

  if (valid_) {
    // Within 5% on period and 2 percentage points on duty counts as the same
    // command being held.
    const bool sameFreq =
        absDiff(frequencyHz_, lastFrequencyHz_) * 20u <= lastFrequencyHz_;
    const bool sameDuty = absDiff(duty_, lastDuty_) <= 20;
    if (sameFreq && sameDuty) {
      if (stableRuns_ < 255) stableRuns_++;
    } else {
      stableRuns_ = 0;
    }
  }

  lastFrequencyHz_ = frequencyHz_;
  lastDuty_ = duty_;
  valid_ = true;
}

void PwmMeter::onEdge(uint32_t nowUs, bool level) {
  edgeCount_++;
  lastEdgeUs_ = nowUs;

  if (level) {
    if (haveRise_) {
      const uint32_t period = nowUs - lastRiseUs_;
      const uint32_t high = lastFallUs_ - lastRiseUs_;
      publish(high, period);
    }
    lastRiseUs_ = nowUs;
    haveRise_ = true;
    return;
  }

  lastFallUs_ = nowUs;
}

void PwmMeter::tick(uint32_t nowUs) {
  if (edgeCount_ == 0) return;
  if ((nowUs - lastEdgeUs_) > kDeadUs) {
    // The line stopped switching. Whatever we last measured is history, and
    // reporting a stale duty here would be worse than reporting nothing.
    valid_ = false;
    stableRuns_ = 0;
    haveRise_ = false;
  }
}

}  // namespace valve
