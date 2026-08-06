#include <string>

#include "console.h"
#include "test_harness.h"

using namespace valve;

namespace {

std::string g_out;

void capture(void *ctx, const char *text) {
  (void)ctx;
  g_out += text;
}

bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

Settings defaultSettings() {
  Settings s;
  loadDefaults(s);
  return s;
}

struct Fixture {
  Settings settings = defaultSettings();
  Mode mode = Mode::Auto;
  CanDecoder decoder;
  ValveController controller;
  SignalHunter hunter;
  PwmMeter meter;
  Console console;

  Fixture()
      : decoder(settings),
        controller(settings),
        console(settings, controller, decoder, hunter, meter, mode) {
    console.setWriter(capture, nullptr);
    controller.begin(0);
  }

  std::string run(const char *line, uint32_t nowMs = 1000) {
    g_out.clear();
    console.handleLine(line, nowMs);
    return g_out;
  }

  // Feed the meter a steady PWM signal so `probe` and `learn` have something
  // to read, exactly as the ISR would on the car.
  void feedPwm(uint16_t hz, unsigned dutyPct, uint32_t startUs = 1000000) {
    const uint32_t periodUs = 1000000u / hz;
    const uint32_t highUs = periodUs * dutyPct / 100u;
    uint32_t t = startUs;
    for (int i = 0; i < 20; ++i) {
      meter.onEdge(t, true);
      meter.onEdge(t + highUs, false);
      t += periodUs;
    }
  }
};

}  // namespace

void run_console_tests() {
  printf("console\n");

  TEST("unknown commands say so instead of doing something");
  {
    Fixture f;
    CHECK(contains(f.run("frobnicate"), "unknown command"));
  }

  TEST("set writes through to the settings struct");
  {
    Fixture f;
    f.run("set smart.openrpm 4200");
    CHECK_EQ(f.settings.smart.openRpm, 4200);
    f.run("set smart.drive on");
    CHECK(f.settings.smart.followDriveSelect);
    f.run("set safety.coolant 70");
    CHECK_EQ(f.settings.safety.minCoolantC, 70);
  }

  TEST("set rejects out of range values and leaves the old one alone");
  {
    Fixture f;
    f.settings.smart.openPedalPct = 60;
    CHECK(contains(f.run("set smart.openpedal 250"), "expected"));
    CHECK_EQ(f.settings.smart.openPedalPct, 60);
    CHECK(contains(f.run("set smart.openrpm banana"), "expected"));
  }

  TEST("learning both end positions is what commissions the controller");
  {
    Fixture f;
    CHECK(!f.settings.actuator.commissioned);

    f.feedPwm(200, 15);
    CHECK(contains(f.run("learn closed"), "learned closed"));
    CHECK(f.settings.actuator.closedLearned);
    CHECK_EQ(f.settings.actuator.pwmHz, 200);
    CHECK(!f.settings.actuator.commissioned);  // only half the story so far

    f.feedPwm(200, 80, 5000000);
    CHECK(contains(f.run("learn open"), "overrides enabled"));
    CHECK(f.settings.actuator.commissioned);
    // 15% and 80% of a 5000 us period, within the meter's resolution.
    CHECK(f.settings.actuator.closedDutyTenths > 130);
    CHECK(f.settings.actuator.closedDutyTenths < 170);
    CHECK(f.settings.actuator.openDutyTenths > 780);
    CHECK(f.settings.actuator.openDutyTenths < 820);
  }

  TEST("learning the same command twice is caught, not commissioned");
  {
    Fixture f;
    f.feedPwm(200, 40);
    f.run("learn closed");
    const std::string out = f.run("learn open");
    CHECK(contains(out, "wrong drive mode"));
    CHECK(!f.settings.actuator.commissioned);
  }

  TEST("learning refuses a signal that is still moving");
  {
    Fixture f;
    // A sweep: every period differs, so nothing ever settles.
    uint32_t t = 1000000;
    for (unsigned pct = 10; pct < 90; pct += 5) {
      const uint32_t period = 5000;
      f.meter.onEdge(t, true);
      f.meter.onEdge(t + period * pct / 100, false);
      t += period;
    }
    CHECK(contains(f.run("learn open"), "still changing"));
    CHECK(!f.settings.actuator.openLearned);
  }

  TEST("learning refuses a dead line");
  {
    Fixture f;
    CHECK(contains(f.run("learn open"), "no PWM"));
    CHECK(!f.settings.actuator.openLearned);
  }

  TEST("probe reports what is on the line");
  {
    Fixture f;
    CHECK(contains(f.run("probe"), "no edges"));
    f.feedPwm(200, 30);
    const std::string out = f.run("probe");
    CHECK(contains(out, "200 Hz"));
    CHECK(contains(out, "steady"));
  }

  TEST("probe flags a line that never settles as possibly not PWM");
  {
    Fixture f;
    // Roughly what LIN looks like: bursts of edges at multiples of the 52 us
    // bit time, separated by milliseconds of idle. Consecutive periods never
    // agree, so nothing ever settles.
    uint32_t t = 1000000;
    for (int frame = 0; frame < 120; ++frame) {
      for (int bit = 0; bit < 12; ++bit) {
        const uint32_t low = 52u * (1 + ((frame + bit) % 4));
        const uint32_t high = 52u * (1 + ((frame * 3 + bit) % 5));
        f.meter.onEdge(t, true);
        f.meter.onEdge(t + high, false);
        t += high + low;
      }
      t += 5000;  // inter-frame idle
    }
    const std::string out = f.run("probe");
    CHECK(contains(out, "CHANGING"));
    CHECK(contains(out, "LIN"));
  }

  TEST("a steady line is not flagged as a data bus");
  {
    Fixture f;
    for (int i = 0; i < 60; ++i) f.feedPwm(200, 40, 1000000 + i * 100000);
    const std::string out = f.run("probe");
    CHECK(contains(out, "steady"));
    CHECK(!contains(out, "LIN"));
  }

  TEST("confirm cannot be forced on before the positions are known");
  {
    Fixture f;
    CHECK(contains(f.run("set confirm on"), "refused"));
    CHECK(!f.settings.actuator.commissioned);
  }

  TEST("duties can be entered by hand and commission the controller");
  {
    Fixture f;
    f.run("set actuator.pwmhz 200");
    f.run("set actuator.closedduty 15");
    CHECK(!f.settings.actuator.commissioned);
    f.run("set actuator.openduty 80.5");
    CHECK(f.settings.actuator.commissioned);
    CHECK_EQ(f.settings.actuator.openDutyTenths, 805);
    CHECK(contains(f.run("set actuator.openduty 120"), "expected"));
  }

  TEST("sig configures and disables a signal");
  {
    Fixture f;
    f.run("sig rpm 0x121 16 16 le 0.25 0");
    const SignalDef &d = f.settings.signals[static_cast<uint8_t>(SignalId::Rpm)];
    CHECK(d.enabled);
    CHECK_EQ(d.canId, 0x121u);
    CHECK_EQ(d.startBit, 16);
    CHECK_EQ(d.bitLength, 16);
    CHECK_EQ(static_cast<int>(d.endian), static_cast<int>(Endian::Little));
    CHECK_NEAR(d.scale, 0.25, 0.0001);

    f.run("sig rpm off");
    CHECK(!f.settings.signals[static_cast<uint8_t>(SignalId::Rpm)].enabled);
  }

  TEST("sig rejects nonsense rather than storing it");
  {
    Fixture f;
    CHECK(contains(f.run("sig rpm 0x121 99 16 le 0.25 0"), "start bit"));
    CHECK(contains(f.run("sig rpm 0x121 16 99 le 0.25 0"), "length"));
    CHECK(contains(f.run("sig rpm 0x121 16 16 middle 0.25 0"), "byte order"));
    CHECK(contains(f.run("sig nonsense 0x121 16 16 le 1 0"), "unknown signal"));
    CHECK(!f.settings.signals[static_cast<uint8_t>(SignalId::Rpm)].enabled);
  }

  TEST("show emits commands you can paste back in");
  {
    Fixture f;
    f.run("sig rpm 0x121 16 16 le 0.25 0");
    f.settings.smart.openRpm = 3777;
    const std::string dump = f.run("show");
    CHECK(contains(dump, "set actuator.pwmhz"));
    CHECK(contains(dump, "set smart.openrpm 3777"));
    CHECK(contains(dump, "sig rpm 0x121 16 16 le 0.25 0"));
    CHECK(contains(dump, "sig speed off"));
  }

  TEST("mode changes are applied and reported");
  {
    Fixture f;
    f.run("mode smart");
    CHECK_EQ(static_cast<int>(f.mode), static_cast<int>(Mode::Smart));
    CHECK(f.console.consumeModeChanged());
    CHECK(!f.console.consumeModeChanged());
    CHECK(contains(f.run("mode sideways"), "usage"));
  }

  TEST("save is a request the platform layer fulfils");
  {
    Fixture f;
    CHECK(!f.console.consumeSaveRequest());
    f.run("save");
    CHECK(f.console.consumeSaveRequest());
    CHECK(!f.console.consumeSaveRequest());
  }

  TEST("bench test is refused while the engine is running");
  {
    Fixture f;
    f.run("set actuator.pwmhz 200");
    f.run("sig rpm 0x121 0 16 le 1 0");
    CanFrame frame;
    frame.id = 0x121;
    frame.dlc = 8;
    for (uint8_t i = 0; i < 8; ++i) frame.data[i] = 0;
    frame.data[0] = 0xE8;  // 1000 rpm
    frame.data[1] = 0x03;
    f.decoder.onFrame(frame, 1000);

    CHECK(contains(f.run("test duty 50", 1000), "refused"));
    CHECK(!f.controller.testActive());
  }

  TEST("bench test runs with the engine off and can be cleared");
  {
    Fixture f;
    // Without a carrier frequency there is nothing to drive.
    CHECK(contains(f.run("test duty 50 5", 1000), "no carrier frequency"));
    f.run("set actuator.pwmhz 200");
    CHECK(contains(f.run("test duty 50 5", 1000), "50.0 %"));
    CHECK(f.controller.testActive());
    f.run("test stock", 1200);
    CHECK(!f.controller.testActive());
    CHECK(contains(f.run("test duty 150", 1000), "0..100"));
  }

  TEST("hunt refuses to mark before it has started");
  {
    Fixture f;
    CHECK(contains(f.run("hunt mark 2000"), "hunt start"));
    f.run("hunt start");
    CHECK(f.console.huntActive());
    CHECK(contains(f.run("hunt mark 2000"), "mark 1 recorded"));
    CHECK(contains(f.run("hunt top"), "at least"));
  }

  TEST("sniff is rate limited so the console stays usable");
  {
    Fixture f;
    f.run("sniff on");
    CHECK(f.console.sniffActive());

    CanFrame frame;
    frame.id = 0x123;
    frame.dlc = 2;
    frame.data[0] = 0xDE;
    frame.data[1] = 0xAD;

    g_out.clear();
    for (int i = 0; i < 20; ++i) f.console.onFrame(frame, 2000);
    const size_t oneWindow = g_out.size();
    CHECK(contains(g_out, "0x123 [2] DE AD"));

    for (int i = 0; i < 20; ++i) f.console.onFrame(frame, 2010);
    CHECK_EQ(g_out.size(), oneWindow);

    f.console.onFrame(frame, 2100);
    CHECK(g_out.size() > oneWindow);

    f.run("sniff off");
    CHECK(!f.console.sniffActive());
  }

  TEST("turning the bus off disables SMART and says so");
  {
    Fixture f;
    f.run("mode smart");
    CHECK_EQ(static_cast<int>(f.mode), static_cast<int>(Mode::Smart));
    CHECK(contains(f.run("set can off"), "not fitted"));
    CHECK(!f.settings.canFitted);
    // The live mode must not be left parked somewhere unreachable.
    CHECK_EQ(static_cast<int>(f.mode), static_cast<int>(Mode::Auto));
    CHECK(contains(f.run("mode smart"), "needs bus data"));
    CHECK_EQ(static_cast<int>(f.mode), static_cast<int>(Mode::Auto));
    CHECK(contains(f.run("set default smart"), "needs bus data"));
    CHECK(contains(f.run("show"), "set can off"));
  }

  TEST("a no-bus install is not nagged about CAN commissioning");
  {
    Fixture f;
    f.run("set can off");
    g_out.clear();
    f.console.greet();
    CHECK(contains(g_out, "no CAN tap"));
    CHECK(!contains(g_out, "CAN signals not fully configured"));
    // It is still told about the polarity, which matters either way.
    CHECK(contains(g_out, "actuator not commissioned"));
  }

  TEST("greeting warns about an uncommissioned install");
  {
    Fixture f;
    g_out.clear();
    f.console.greet();
    CHECK(contains(g_out, "actuator not commissioned"));
    CHECK(contains(g_out, "CAN signals not fully configured"));
  }
}
