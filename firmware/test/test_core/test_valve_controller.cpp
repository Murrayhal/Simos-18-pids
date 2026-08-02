#include "test_harness.h"
#include "valve_controller.h"

using namespace valve;

namespace {

// A car that satisfies every interlock: warm, running, on the bus.
VehicleState healthyState(uint16_t rpm = 1000, uint8_t pedal = 0) {
  VehicleState vs;
  vs.rpmValid = true;
  vs.rpm = rpm;
  vs.pedalValid = true;
  vs.pedalPct = pedal;
  vs.coolantValid = true;
  vs.coolantC = 90;
  vs.speedValid = true;
  vs.speedKph = 60;
  vs.anyValid = true;
  return vs;
}

// Commissioned settings with the timing interlocks out of the way, so each
// test only exercises the thing it is about.
Settings commissioned() {
  Settings s;
  loadDefaults(s);
  s.actuator.pwmHz = 200;
  s.actuator.openDutyTenths = 800;   // 80.0%
  s.actuator.closedDutyTenths = 150; // 15.0%
  s.actuator.openLearned = true;
  s.actuator.closedLearned = true;
  s.actuator.commissioned = true;
  s.safety.startupQuietMs = 0;
  s.safety.minDwellMs = 0;
  return s;
}

}  // namespace

void run_valve_controller_tests() {
  printf("valve_controller\n");

  TEST("an uncommissioned controller never drives the actuator");
  {
    Settings s = commissioned();
    s.actuator.commissioned = false;
    ValveController c(s);
    c.begin(0);
    const ControllerOutput out = c.update(100, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Stock));
    CHECK(!out.interceptRelay);
    CHECK_EQ(out.commandDuty, 0);
    CHECK_EQ(static_cast<int>(out.lockout),
             static_cast<int>(Lockout::NotCommissioned));
  }

  TEST("AUTO hands everything back to the ECU");
  {
    Settings s = commissioned();
    ValveController c(s);
    c.begin(0);
    const ControllerOutput out = c.update(100, Mode::Auto, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Stock));
    CHECK(!out.interceptRelay);
    CHECK(!out.overrideActive);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::None));
  }

  TEST("OPEN and QUIET replay the learned end-position commands");
  {
    Settings s = commissioned();
    ValveController c(s);
    c.begin(0);
    ControllerOutput out = c.update(100, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
    CHECK(out.interceptRelay);
    CHECK_EQ(out.commandDuty, 800);
    CHECK(out.overrideActive);

    out = c.update(200, Mode::Quiet, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));
    CHECK(out.interceptRelay);
    CHECK_EQ(out.commandDuty, 150);
  }

  TEST("a car whose duties run the other way needs no code change");
  {
    // Nothing in the controller assumes which end of the range is loud; the
    // learned numbers carry it.
    Settings s = commissioned();
    s.actuator.openDutyTenths = 100;
    s.actuator.closedDutyTenths = 900;
    ValveController c(s);
    c.begin(0);
    CHECK_EQ(c.update(100, Mode::Open, healthyState(), 0).commandDuty, 100);
    CHECK_EQ(c.update(200, Mode::Quiet, healthyState(), 0).commandDuty, 900);
  }

  TEST("a cold engine refuses the override");
  {
    Settings s = commissioned();
    s.safety.minCoolantC = 60;
    ValveController c(s);
    c.begin(0);
    VehicleState vs = healthyState();
    vs.coolantC = 30;
    const ControllerOutput out = c.update(100, Mode::Open, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Stock));
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::Warmup));
  }

  TEST("a missing coolant signal counts as cold, not as warm");
  {
    Settings s = commissioned();
    s.safety.minCoolantC = 60;
    ValveController c(s);
    c.begin(0);
    VehicleState vs = healthyState();
    vs.coolantValid = false;
    const ControllerOutput out = c.update(100, Mode::Open, vs, 0);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::Warmup));
  }

  TEST("a stationary engine refuses the override");
  {
    Settings s = commissioned();
    ValveController c(s);
    c.begin(0);
    VehicleState vs = healthyState(0);
    const ControllerOutput out = c.update(100, Mode::Open, vs, 0);
    CHECK_EQ(static_cast<int>(out.lockout),
             static_cast<int>(Lockout::EngineOff));
  }

  TEST("a dead bus refuses the override when configured to require it");
  {
    Settings s = commissioned();
    s.safety.requireCanForOverride = true;
    ValveController c(s);
    c.begin(0);
    VehicleState vs;  // nothing valid at all
    const ControllerOutput out = c.update(100, Mode::Open, vs, 0);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::CanLost));
  }

  TEST("the post-start quiet window expires");
  {
    Settings s = commissioned();
    s.safety.startupQuietMs = 5000;
    ValveController c(s);
    c.begin(0);
    ControllerOutput out = c.update(1000, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.lockout),
             static_cast<int>(Lockout::StartupDelay));
    out = c.update(4000, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.lockout),
             static_cast<int>(Lockout::StartupDelay));
    out = c.update(6500, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::None));
    CHECK(out.overrideActive);
  }

  TEST("battery sense gates the override when fitted");
  {
    Settings s = commissioned();
    s.safety.batterySenseFitted = true;
    s.safety.minBatteryMv = 11000;
    s.safety.maxBatteryMv = 15500;
    ValveController c(s);
    c.begin(0);
    ControllerOutput out = c.update(100, Mode::Open, healthyState(), 10200);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::Voltage));
    out = c.update(200, Mode::Open, healthyState(), 13800);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::None));
  }

  TEST("SMART opens and closes with hysteresis, not on a single threshold");
  {
    Settings s = commissioned();
    s.smart.openRpm = 3200;
    s.smart.closeRpm = 2600;
    s.smart.openPedalPct = 60;
    s.smart.closePedalPct = 40;
    ValveController c(s);
    c.begin(0);

    ControllerOutput out = c.update(100, Mode::Smart, healthyState(2000), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));

    out = c.update(200, Mode::Smart, healthyState(3300), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));

    // Between the thresholds it must hold, not chatter.
    out = c.update(300, Mode::Smart, healthyState(2900), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));

    out = c.update(400, Mode::Smart, healthyState(2500), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));

    // Pedal alone can open it at low revs.
    out = c.update(500, Mode::Smart, healthyState(2000, 80), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
  }

  TEST("SMART follows drive select when asked to");
  {
    Settings s = commissioned();
    s.smart.followDriveSelect = true;
    s.smart.dynamicRawValue = 3;
    ValveController c(s);
    c.begin(0);

    VehicleState vs = healthyState(1200);
    vs.driveSelectValid = true;
    vs.driveSelectRaw = 1;
    ControllerOutput out = c.update(100, Mode::Smart, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));

    vs.driveSelectRaw = 3;
    out = c.update(200, Mode::Smart, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
  }

  TEST("the low speed quiet window yields to a real throttle input");
  {
    Settings s = commissioned();
    s.smart.quietBelowKph = 30;
    s.smart.openRpm = 3200;
    s.smart.openPedalPct = 60;
    ValveController c(s);
    c.begin(0);

    VehicleState vs = healthyState(4000, 10);
    vs.speedKph = 15;
    ControllerOutput out = c.update(100, Mode::Smart, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));

    // Same speed, but the driver has actually asked for it.
    vs.pedalPct = 90;
    out = c.update(200, Mode::Smart, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
  }

  TEST("minimum dwell stops the flap hammering");
  {
    Settings s = commissioned();
    s.safety.minDwellMs = 1000;
    ValveController c(s);
    c.begin(0);

    ControllerOutput out = c.update(2000, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));

    // Asking for the other extreme immediately is held off.
    out = c.update(2200, Mode::Quiet, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));

    out = c.update(3100, Mode::Quiet, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));
  }

  TEST("the dwell timer does not delay the first move after a reset");
  {
    Settings s = commissioned();
    s.safety.minDwellMs = 5000;
    ValveController c(s);
    c.begin(0);
    // A controller that resets while the car is being driven must honour the
    // selected mode straight away, not sit on its hands for a dwell period.
    const ControllerOutput out = c.update(50, Mode::Open, healthyState(), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
    CHECK(out.overrideActive);
  }

  TEST("handing control back never waits for the dwell timer");
  {
    Settings s = commissioned();
    s.safety.minDwellMs = 5000;
    ValveController c(s);
    c.begin(0);

    ControllerOutput out = c.update(1000, Mode::Open, healthyState(), 0);
    CHECK(out.overrideActive);

    // Engine stops: the interlock must act immediately, not in five seconds.
    out = c.update(1100, Mode::Open, healthyState(0), 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Stock));
    CHECK(!out.interceptRelay);
    CHECK_EQ(out.commandDuty, 0);
  }

  TEST("with no CAN tap the bus interlocks are skipped, not failed");
  {
    Settings s = commissioned();
    s.canFitted = false;
    // These would all refuse an override on a bus-equipped car and would all
    // be permanently unsatisfiable without one.
    s.safety.requireCanForOverride = true;
    s.safety.requireEngineRunning = true;
    s.safety.minCoolantC = 60;
    s.safety.startupQuietMs = 5000;
    ValveController c(s);
    c.begin(0);

    VehicleState vs;  // nothing valid, because there is no bus
    ControllerOutput out = c.update(100, Mode::Open, vs, 0);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::None));
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Open));
    CHECK(out.interceptRelay);

    out = c.update(200, Mode::Quiet, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Closed));

    out = c.update(300, Mode::Auto, vs, 0);
    CHECK_EQ(static_cast<int>(out.target), static_cast<int>(Target::Stock));
  }

  TEST("no CAN tap still does not bypass commissioning or the battery window");
  {
    Settings s = commissioned();
    s.canFitted = false;
    s.actuator.commissioned = false;
    ValveController c(s);
    c.begin(0);
    ControllerOutput out = c.update(100, Mode::Open, VehicleState(), 0);
    CHECK_EQ(static_cast<int>(out.lockout),
             static_cast<int>(Lockout::NotCommissioned));

    s.actuator.commissioned = true;
    s.safety.batterySenseFitted = true;
    out = c.update(200, Mode::Open, VehicleState(), 9000);
    CHECK_EQ(static_cast<int>(out.lockout), static_cast<int>(Lockout::Voltage));
  }

  TEST("a bench test drives the actuator and then expires");
  {
    Settings s = commissioned();
    s.actuator.commissioned = false;  // used before anything is commissioned
    ValveController c(s);
    c.begin(0);

    c.startTest(true, 650, 5000, 1000);
    ControllerOutput out = c.update(1500, Mode::Auto, VehicleState(), 0);
    CHECK(out.testActive);
    CHECK(out.interceptRelay);
    CHECK_EQ(out.commandDuty, 650);

    out = c.update(6500, Mode::Auto, VehicleState(), 0);
    CHECK(!out.testActive);
    CHECK(!out.interceptRelay);
    CHECK_EQ(out.commandDuty, 0);
  }

  TEST("stopping a test releases the outputs at once");
  {
    Settings s = commissioned();
    ValveController c(s);
    c.begin(0);
    c.startTest(true, 500, 30000, 1000);
    CHECK_EQ(c.update(1100, Mode::Auto, VehicleState(), 0).commandDuty, 500);
    c.stopTest();
    const ControllerOutput out = c.update(1200, Mode::Auto, VehicleState(), 0);
    CHECK_EQ(out.commandDuty, 0);
    CHECK(!out.interceptRelay);
  }
}
