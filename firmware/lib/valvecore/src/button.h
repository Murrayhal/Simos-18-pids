// Debounced single-button input with short and long press.
#pragma once

#include <stdint.h>

namespace valve {

enum class ButtonEvent : uint8_t { None = 0, Short, Long };

class Button {
 public:
  Button(uint16_t debounceMs = 25, uint16_t longPressMs = 1200,
         uint32_t stuckMs = 30000);

  // `pressed` is the already-de-inverted level: true means the button is down.
  ButtonEvent update(uint32_t nowMs, bool pressed);

  bool isDown() const { return stable_; }
  // True when the input has been held long enough that we assume a shorted
  // wire or a jammed button and stopped believing it.
  bool isStuck() const { return stuck_; }

 private:
  const uint16_t debounceMs_;
  const uint16_t longPressMs_;
  const uint32_t stuckMs_;

  bool raw_;
  bool stable_;
  bool longFired_;
  bool stuck_;
  bool initialised_;
  uint32_t lastEdgeMs_;
  uint32_t downSinceMs_;
};

}  // namespace valve
