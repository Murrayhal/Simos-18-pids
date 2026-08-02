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
  Console console;

  Fixture()
      : decoder(settings),
        controller(settings),
        console(settings, controller, decoder, hunter, mode) {
    console.setWriter(capture, nullptr);
    controller.begin(0);
  }

  std::string run(const char *line, uint32_t nowMs = 1000) {
    g_out.clear();
    console.handleLine(line, nowMs);
    return g_out;
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

  TEST("declaring the polarity is what commissions the controller");
  {
    Fixture f;
    CHECK(!f.settings.polarityConfirmed);
    f.run("set polarity opens");
    CHECK(f.settings.polarityConfirmed);
    CHECK(!f.settings.energizedClosesValve);
    f.run("set polarity closes");
    CHECK(f.settings.energizedClosesValve);
    CHECK(contains(f.run("set polarity sideways"), "usage"));
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
    f.run("sig rpm 0x121 0 16 le 1 0");
    CanFrame frame;
    frame.id = 0x121;
    frame.dlc = 8;
    for (uint8_t i = 0; i < 8; ++i) frame.data[i] = 0;
    frame.data[0] = 0xE8;  // 1000 rpm
    frame.data[1] = 0x03;
    f.decoder.onFrame(frame, 1000);

    CHECK(contains(f.run("test energize", 1000), "refused"));
    CHECK(!f.controller.testActive());
  }

  TEST("bench test runs with the engine off and can be cleared");
  {
    Fixture f;
    CHECK(contains(f.run("test energize 5", 1000), "ENERGISED"));
    CHECK(f.controller.testActive());
    f.run("test stock", 1200);
    CHECK(!f.controller.testActive());
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

  TEST("greeting warns about an uncommissioned install");
  {
    Fixture f;
    g_out.clear();
    f.console.greet();
    CHECK(contains(g_out, "polarity not commissioned"));
    CHECK(contains(g_out, "CAN signals not fully configured"));
  }
}
