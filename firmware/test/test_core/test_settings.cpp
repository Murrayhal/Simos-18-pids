#include "settings.h"
#include "test_harness.h"

using namespace valve;

void run_settings_tests() {
  printf("settings\n");

  TEST("defaults are the safe ones");
  {
    Settings s;
    loadDefaults(s);
    CHECK(!s.polarityConfirmed);
    CHECK(s.canFitted);
    CHECK_EQ(static_cast<int>(s.defaultMode), static_cast<int>(Mode::Auto));
    CHECK(s.safety.requireCanForOverride);
    CHECK(s.safety.requireEngineRunning);
    for (uint8_t i = 0; i < kSignalCount; ++i) CHECK(!s.signals[i].enabled);
    CHECK(!signalsCommissioned(s));
  }

  TEST("there is nothing to commission without a bus");
  {
    Settings s;
    loadDefaults(s);
    s.canFitted = false;
    CHECK(signalsCommissioned(s));
  }

  TEST("SMART is unavailable without a bus");
  {
    Settings s;
    loadDefaults(s);
    CHECK(modeAvailable(s, Mode::Smart));
    s.canFitted = false;
    CHECK(!modeAvailable(s, Mode::Smart));
    CHECK(modeAvailable(s, Mode::Auto));
    CHECK(modeAvailable(s, Mode::Open));
    CHECK(modeAvailable(s, Mode::Quiet));
  }

  TEST("the button cycle covers every mode when a bus is fitted");
  {
    Settings s;
    loadDefaults(s);
    Mode m = Mode::Auto;
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Smart));
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Open));
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Quiet));
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Auto));
  }

  TEST("the button cycle skips SMART without a bus");
  {
    Settings s;
    loadDefaults(s);
    s.canFitted = false;
    Mode m = Mode::Auto;
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Open));
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Quiet));
    m = nextMode(s, m);
    CHECK_EQ(static_cast<int>(m), static_cast<int>(Mode::Auto));

    // Even if something left us parked in SMART, the cycle escapes it.
    CHECK_EQ(static_cast<int>(nextMode(s, Mode::Smart)),
             static_cast<int>(Mode::Open));
  }

  TEST("commissioning tracks which signals the configuration actually needs");
  {
    Settings s;
    loadDefaults(s);
    s.safety.minCoolantC = -273;  // warm-up check disabled
    s.signals[static_cast<uint8_t>(SignalId::Rpm)].enabled = true;
    CHECK(signalsCommissioned(s));

    s.smart.quietBelowKph = 30;  // now it needs road speed
    CHECK(!signalsCommissioned(s));
    s.signals[static_cast<uint8_t>(SignalId::Speed)].enabled = true;
    CHECK(signalsCommissioned(s));

    s.smart.followDriveSelect = true;  // and drive select
    CHECK(!signalsCommissioned(s));
    s.signals[static_cast<uint8_t>(SignalId::DriveSelect)].enabled = true;
    CHECK(signalsCommissioned(s));
  }
}
