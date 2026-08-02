#include "led_indicator.h"
#include "test_harness.h"

using namespace valve;

namespace {

ControllerOutput out(Lockout lockout = Lockout::None, bool test = false) {
  ControllerOutput o;
  o.lockout = lockout;
  o.testActive = test;
  return o;
}

// True if the LED is lit at some point and dark at another over one second.
bool blinks(LedIndicator &led, Mode mode, const ControllerOutput &o) {
  bool sawOn = false, sawOff = false;
  for (uint32_t t = 0; t < 1000; t += 10) {
    const LedOut c = led.update(t, mode, o);
    if (c.any()) sawOn = true; else sawOff = true;
  }
  return sawOn && sawOff;
}

}  // namespace

void run_led_tests() {
  printf("led_indicator\n");

  TEST("each mode has its own steady colour");
  {
    LedIndicator led;
    const LedOut a = led.update(0, Mode::Auto, out());
    CHECK(a.g && !a.r && !a.b);
    const LedOut s = led.update(0, Mode::Smart, out());
    CHECK(s.b && !s.r && !s.g);
    const LedOut o = led.update(0, Mode::Open, out());
    CHECK(o.r && !o.g && !o.b);
    const LedOut q = led.update(0, Mode::Quiet, out());
    CHECK(q.g && q.b && !q.r);
  }

  TEST("a steady colour really is steady");
  {
    LedIndicator led;
    CHECK(!blinks(led, Mode::Open, out()));
  }

  TEST("a refused override blinks the mode colour");
  {
    LedIndicator led;
    CHECK(blinks(led, Mode::Open, out(Lockout::Warmup)));
    bool sawRedOnly = true;
    for (uint32_t t = 0; t < 1000; t += 10) {
      const LedOut c = led.update(t, Mode::Open, out(Lockout::Warmup));
      if (c.any() && (c.g || c.b)) sawRedOnly = false;
    }
    CHECK(sawRedOnly);
  }

  TEST("an uncommissioned controller shows amber regardless of mode");
  {
    LedIndicator led;
    bool sawAmber = false;
    for (uint32_t t = 0; t < 1000; t += 10) {
      const LedOut c = led.update(t, Mode::Smart, out(Lockout::NotCommissioned));
      if (c.r && c.g && !c.b) sawAmber = true;
      CHECK(!c.b);
    }
    CHECK(sawAmber);
  }

  TEST("a bench test shows white");
  {
    LedIndicator led;
    bool sawWhite = false;
    for (uint32_t t = 0; t < 400; t += 10) {
      const LedOut c = led.update(t, Mode::Auto, out(Lockout::None, true));
      if (c.r && c.g && c.b) sawWhite = true;
    }
    CHECK(sawWhite);
  }

  TEST("an acknowledgement flash runs a fixed number of times then stops");
  {
    LedIndicator led;
    led.requestFlash(3, 100, 100);
    int transitions = 0;
    bool prev = false;
    uint32_t t = 0;
    for (; t < 1200; t += 10) {
      const LedOut c = led.update(t, Mode::Auto, out());
      const bool on = c.any();
      if (on != prev) transitions++;
      prev = on;
    }
    // Three flashes: on/off three times, then it settles on the mode colour.
    CHECK(transitions >= 5);
    const LedOut settled = led.update(t + 100, Mode::Auto, out());
    CHECK(settled.g && !settled.r && !settled.b);
  }
}
