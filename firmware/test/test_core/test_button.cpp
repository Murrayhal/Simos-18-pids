#include "button.h"
#include "test_harness.h"

using namespace valve;

namespace {

// Holds a level for a span of time, returning the last event seen.
ButtonEvent hold(Button &b, uint32_t &now, bool pressed, uint32_t ms,
                 uint32_t stepMs = 5) {
  ButtonEvent seen = ButtonEvent::None;
  const uint32_t end = now + ms;
  while (now < end) {
    const ButtonEvent e = b.update(now, pressed);
    if (e != ButtonEvent::None) seen = e;
    now += stepMs;
  }
  return seen;
}

}  // namespace

void run_button_tests() {
  printf("button\n");

  TEST("a clean short press reports on release");
  {
    Button b(25, 1200, 30000);
    uint32_t now = 0;
    b.update(now, false);
    CHECK_EQ(static_cast<int>(hold(b, now, false, 100)),
             static_cast<int>(ButtonEvent::None));
    CHECK_EQ(static_cast<int>(hold(b, now, true, 200)),
             static_cast<int>(ButtonEvent::None));
    CHECK_EQ(static_cast<int>(hold(b, now, false, 100)),
             static_cast<int>(ButtonEvent::Short));
  }

  TEST("bounce shorter than the debounce window is ignored");
  {
    Button b(25, 1200, 30000);
    uint32_t now = 0;
    b.update(now, false);
    hold(b, now, false, 100);
    // 10 ms of chatter, then back to released.
    ButtonEvent seen = ButtonEvent::None;
    for (int i = 0; i < 5; ++i) {
      const ButtonEvent e = b.update(now, i % 2 == 0);
      if (e != ButtonEvent::None) seen = e;
      now += 2;
    }
    seen = hold(b, now, false, 100);
    CHECK_EQ(static_cast<int>(seen), static_cast<int>(ButtonEvent::None));
    CHECK(!b.isDown());
  }

  TEST("a long press fires once while held and suppresses the short press");
  {
    Button b(25, 1200, 30000);
    uint32_t now = 0;
    b.update(now, false);
    hold(b, now, false, 100);
    CHECK_EQ(static_cast<int>(hold(b, now, true, 1500)),
             static_cast<int>(ButtonEvent::Long));
    // Still held: no second Long.
    CHECK_EQ(static_cast<int>(hold(b, now, true, 2000)),
             static_cast<int>(ButtonEvent::None));
    // Release must not also count as a short press.
    CHECK_EQ(static_cast<int>(hold(b, now, false, 200)),
             static_cast<int>(ButtonEvent::None));
  }

  TEST("a button already down at power-up is ignored until released");
  {
    Button b(25, 1200, 30000);
    uint32_t now = 0;
    CHECK_EQ(static_cast<int>(b.update(now, true)),
             static_cast<int>(ButtonEvent::None));
    CHECK(b.isStuck());
    now += 10;
    CHECK_EQ(static_cast<int>(hold(b, now, true, 3000)),
             static_cast<int>(ButtonEvent::None));
    CHECK_EQ(static_cast<int>(hold(b, now, false, 200)),
             static_cast<int>(ButtonEvent::None));
    CHECK(!b.isStuck());
    // And it works normally afterwards.
    hold(b, now, true, 200);
    CHECK_EQ(static_cast<int>(hold(b, now, false, 200)),
             static_cast<int>(ButtonEvent::Short));
  }

  TEST("a jammed input stops generating events");
  {
    Button b(25, 1200, 5000);
    uint32_t now = 0;
    b.update(now, false);
    hold(b, now, false, 100);
    CHECK_EQ(static_cast<int>(hold(b, now, true, 1500, 10)),
             static_cast<int>(ButtonEvent::Long));
    hold(b, now, true, 8000, 25);
    CHECK(b.isStuck());
    CHECK_EQ(static_cast<int>(hold(b, now, false, 200)),
             static_cast<int>(ButtonEvent::None));
  }
}
