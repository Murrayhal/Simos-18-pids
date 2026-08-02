#include "button.h"

namespace valve {

Button::Button(uint16_t debounceMs, uint16_t longPressMs, uint32_t stuckMs)
    : debounceMs_(debounceMs),
      longPressMs_(longPressMs),
      stuckMs_(stuckMs),
      raw_(false),
      stable_(false),
      longFired_(false),
      stuck_(false),
      initialised_(false),
      lastEdgeMs_(0),
      downSinceMs_(0) {}

ButtonEvent Button::update(uint32_t nowMs, bool pressed) {
  if (!initialised_) {
    initialised_ = true;
    raw_ = pressed;
    stable_ = pressed;
    lastEdgeMs_ = nowMs;
    // A button that is already down at power-up is ignored until it is
    // released, so a stuck switch cannot cycle modes on every boot.
    if (pressed) {
      stuck_ = true;
    }
    return ButtonEvent::None;
  }

  if (pressed != raw_) {
    raw_ = pressed;
    lastEdgeMs_ = nowMs;
    return ButtonEvent::None;
  }

  if ((nowMs - lastEdgeMs_) < debounceMs_) return ButtonEvent::None;
  if (raw_ == stable_) {
    // Level held. Check for the long press and for a jammed input.
    if (stable_) {
      const uint32_t held = nowMs - downSinceMs_;
      if (!stuck_ && held >= stuckMs_) {
        stuck_ = true;
        return ButtonEvent::None;
      }
      if (!stuck_ && !longFired_ && held >= longPressMs_) {
        longFired_ = true;
        return ButtonEvent::Long;
      }
    }
    return ButtonEvent::None;
  }

  // Debounced edge.
  stable_ = raw_;
  if (stable_) {
    downSinceMs_ = nowMs;
    longFired_ = false;
    return ButtonEvent::None;
  }

  // Release.
  const bool wasStuck = stuck_;
  stuck_ = false;
  if (wasStuck || longFired_) return ButtonEvent::None;
  return ButtonEvent::Short;
}

}  // namespace valve
