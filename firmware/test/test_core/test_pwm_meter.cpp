#include "pwm_meter.h"
#include "test_harness.h"

using namespace valve;

namespace {

// Drives `cycles` periods of a clean PWM signal into the meter, returning the
// timestamp just past the end.
uint32_t feed(PwmMeter &m, uint32_t startUs, uint16_t hz, unsigned dutyPct,
              int cycles) {
  const uint32_t period = 1000000u / hz;
  const uint32_t high = period * dutyPct / 100u;
  uint32_t t = startUs;
  for (int i = 0; i < cycles; ++i) {
    m.onEdge(t, true);
    m.onEdge(t + high, false);
    t += period;
  }
  return t;
}

}  // namespace

void run_pwm_meter_tests() {
  printf("pwm_meter\n");

  TEST("a clean signal is measured and declared stable");
  {
    PwmMeter m;
    feed(m, 1000000, 200, 40, 20);
    CHECK(m.valid());
    CHECK(m.stable());
    CHECK_EQ(m.frequencyHz(), 200);
    CHECK(m.duty() > 380 && m.duty() < 420);
  }

  TEST("nothing is reported before any edges arrive");
  {
    PwmMeter m;
    CHECK(!m.valid());
    CHECK(!m.stable());
    CHECK_EQ(m.edgeCount(), 0u);
  }

  TEST("one period is not enough to call it stable");
  {
    PwmMeter m;
    feed(m, 1000000, 200, 40, 2);
    CHECK(m.valid());
    CHECK(!m.stable());
    CHECK(m.unsettled());
  }

  TEST("a changing command is measured but never called stable");
  {
    PwmMeter m;
    uint32_t t = 1000000;
    const uint32_t period = 5000;
    for (unsigned pct = 10; pct < 90; pct += 10) {
      m.onEdge(t, true);
      m.onEdge(t + period * pct / 100u, false);
      t += period;
    }
    CHECK(m.valid());
    CHECK(!m.stable());
    CHECK(m.unsettled());
  }

  TEST("settling after a change is reported as stable again");
  {
    PwmMeter m;
    uint32_t t = feed(m, 1000000, 200, 20, 20);
    CHECK(m.stable());
    CHECK(m.duty() < 250);

    t = feed(m, t, 200, 75, 3);
    CHECK(!m.stable());  // mid-move

    feed(m, t, 200, 75, 20);
    CHECK(m.stable());
    CHECK(m.duty() > 730 && m.duty() < 770);
  }

  TEST("a line that stops switching is invalidated, not left stale");
  {
    PwmMeter m;
    const uint32_t end = feed(m, 1000000, 200, 40, 20);
    CHECK(m.stable());

    m.tick(end + 1000);
    CHECK(m.valid());  // one period later, still live

    m.tick(end + 500000);  // half a second of silence
    CHECK(!m.valid());
    CHECK(!m.stable());
    // The edges we did see are still counted, which is how `probe` tells
    // "never driven" apart from "driven and now parked".
    CHECK(m.edgeCount() > 0u);
  }

  TEST("a missed edge does not produce a bogus reading");
  {
    PwmMeter m;
    uint32_t t = feed(m, 1000000, 200, 40, 20);
    const DutyTenths before = m.duty();
    // A rising edge with no fall in between, as if an edge were lost.
    m.onEdge(t, true);
    m.onEdge(t + 5000, true);
    // Duty can never exceed 100%, whatever the input does.
    CHECK(m.duty() <= kDutyMax);
    CHECK(before <= kDutyMax);
  }

  TEST("measurement survives the micros wrap");
  {
    PwmMeter m;
    const uint32_t nearMax = 0xFFFFF000u;
    feed(m, nearMax, 200, 40, 20);
    CHECK(m.valid());
    CHECK(m.stable());
    CHECK_EQ(m.frequencyHz(), 200);
    CHECK(m.duty() > 380 && m.duty() < 420);
  }

  TEST("implausible periods are rejected rather than published");
  {
    PwmMeter m;
    // 0.5 Hz: far below anything an actuator command would use.
    m.onEdge(0, true);
    m.onEdge(1000000, false);
    m.onEdge(2000000, true);
    CHECK(!m.valid());
  }

  TEST("reset clears everything");
  {
    PwmMeter m;
    feed(m, 1000000, 200, 40, 20);
    CHECK(m.valid());
    m.reset();
    CHECK(!m.valid());
    CHECK(!m.stable());
    CHECK_EQ(m.edgeCount(), 0u);
    CHECK_EQ(m.duty(), 0);
  }
}
