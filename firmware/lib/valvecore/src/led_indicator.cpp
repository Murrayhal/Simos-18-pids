#include "led_indicator.h"

namespace valve {
namespace {

bool blink(uint32_t nowMs, uint16_t periodMs) {
  if (periodMs == 0) return true;
  return (nowMs % periodMs) < (periodMs / 2u);
}

}  // namespace

LedIndicator::LedIndicator()
    : flashesLeft_(0),
      flashOnMs_(120),
      flashOffMs_(120),
      flashPhaseMs_(0),
      flashOn_(false),
      flashActive_(false) {}

void LedIndicator::requestFlash(uint8_t count, uint16_t onMs, uint16_t offMs) {
  flashesLeft_ = count;
  flashOnMs_ = onMs;
  flashOffMs_ = offMs;
  flashActive_ = false;
}

LedOut LedIndicator::colourFor(Mode mode) {
  LedOut c;
  switch (mode) {
    case Mode::Auto: c.g = true; break;
    case Mode::Smart: c.b = true; break;
    case Mode::Open: c.r = true; break;
    case Mode::Quiet: c.g = true; c.b = true; break;
    default: break;
  }
  return c;
}

LedOut LedIndicator::update(uint32_t nowMs, Mode mode,
                            const ControllerOutput &out) {
  // Acknowledgement flashes win over everything.
  if (flashesLeft_ > 0) {
    if (!flashActive_) {
      flashActive_ = true;
      flashOn_ = true;
      flashPhaseMs_ = nowMs;
    }
    const uint32_t elapsed = nowMs - flashPhaseMs_;
    const uint16_t limit = flashOn_ ? flashOnMs_ : flashOffMs_;
    if (elapsed >= limit) {
      flashPhaseMs_ = nowMs;
      if (flashOn_) {
        flashOn_ = false;
      } else {
        flashOn_ = true;
        if (--flashesLeft_ == 0) {
          flashActive_ = false;
        }
      }
    }
    LedOut white;
    white.r = white.g = white.b = flashOn_ && flashesLeft_ > 0;
    return white;
  }

  if (out.testActive) {
    const bool on = blink(nowMs, 200);
    LedOut white;
    white.r = white.g = white.b = on;
    return white;
  }

  if (out.lockout == Lockout::NotCommissioned) {
    const bool on = blink(nowMs, 200);
    LedOut amber;
    amber.r = on;
    amber.g = on;
    return amber;
  }

  LedOut c = colourFor(mode);
  if (out.lockout != Lockout::None) {
    const bool on = blink(nowMs, 500);
    if (!on) return LedOut();
  }
  return c;
}

}  // namespace valve
