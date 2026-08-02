// Blink patterns for the dashboard LED.
//
// Colour tells you the mode, blinking tells you something is wrong or pending:
//
//   AUTO   green solid          factory behaviour, controller stood down
//   SMART  blue solid           controller deciding from bus data
//   OPEN   red solid            forced open
//   QUIET  cyan solid           forced closed
//
//   mode colour, 2 Hz blink     override refused, see the lockout on `status`
//   amber, 5 Hz blink           polarity never commissioned, nothing will move
//   white, 5 Hz blink           bench test driving the outputs
//   white, 3 flashes            acknowledgement (default mode saved)
//
// A single-colour LED still works: any lit channel means "on", so the blink
// codes carry the information and you lose only the colour.
#pragma once

#include "valve_controller.h"
#include "valve_types.h"

namespace valve {

struct LedOut {
  bool r = false;
  bool g = false;
  bool b = false;

  bool any() const { return r || g || b; }
  bool operator==(const LedOut &o) const {
    return r == o.r && g == o.g && b == o.b;
  }
};

class LedIndicator {
 public:
  LedIndicator();

  // Queue an acknowledgement flash. Takes priority over everything until done.
  void requestFlash(uint8_t count, uint16_t onMs = 120, uint16_t offMs = 120);

  LedOut update(uint32_t nowMs, Mode mode, const ControllerOutput &out);

 private:
  static LedOut colourFor(Mode mode);

  uint8_t flashesLeft_;
  uint16_t flashOnMs_;
  uint16_t flashOffMs_;
  uint32_t flashPhaseMs_;
  bool flashOn_;
  bool flashActive_;
};

}  // namespace valve
