#include "valve_controller.h"

namespace valve {

ValveController::ValveController(const Settings &settings)
    : settings_(settings),
      currentTarget_(Target::Stock),
      lastChangeMs_(0),
      dwellArmed_(false),
      smartOpen_(false),
      engineWasRunning_(false),
      engineStartMs_(0),
      testActive_(false),
      testIntercept_(false),
      testSolenoid_(false),
      testUntilMs_(0),
      started_(false) {}

void ValveController::begin(uint32_t nowMs) {
  currentTarget_ = Target::Stock;
  lastChangeMs_ = nowMs;
  dwellArmed_ = false;
  smartOpen_ = false;
  engineWasRunning_ = false;
  engineStartMs_ = nowMs;
  testActive_ = false;
  last_ = ControllerOutput();
  started_ = true;
}

void ValveController::startTest(bool interceptRelay, bool solenoid,
                                uint32_t durationMs, uint32_t nowMs) {
  testActive_ = true;
  testIntercept_ = interceptRelay;
  testSolenoid_ = solenoid;
  testUntilMs_ = nowMs + durationMs;
}

void ValveController::stopTest() { testActive_ = false; }

void ValveController::updateSmart(const VehicleState &state) {
  const SmartSettings &sm = settings_.smart;

  const bool rpmOpen = state.rpmValid && state.rpm >= sm.openRpm;
  const bool pedalOpen = state.pedalValid && state.pedalPct >= sm.openPedalPct;
  const bool driveOpen = sm.followDriveSelect && state.driveSelectValid &&
                         state.driveSelectRaw == sm.dynamicRawValue;

  // The low-speed quiet window only bites while the driver is off the throttle,
  // so pulling away hard still opens the flap but crawling does not.
  const bool quietZone = sm.quietBelowKph > 0 && state.speedValid &&
                         state.speedKph < sm.quietBelowKph && !pedalOpen;

  if (quietZone) {
    smartOpen_ = false;
    return;
  }

  if (rpmOpen || pedalOpen || driveOpen) {
    smartOpen_ = true;
    return;
  }

  // A signal we do not have cannot hold the flap open, but it must not block
  // closing either, hence the validity guards on the close side.
  const bool rpmClosed = !state.rpmValid || state.rpm <= sm.closeRpm;
  const bool pedalClosed = !state.pedalValid || state.pedalPct <= sm.closePedalPct;
  if (rpmClosed && pedalClosed && !driveOpen) {
    smartOpen_ = false;
  }
  // Anything between the two thresholds leaves smartOpen_ where it was.
}

Target ValveController::desiredTarget(Mode mode, const VehicleState &state) {
  switch (mode) {
    case Mode::Open:
      return Target::Open;
    case Mode::Quiet:
      return Target::Closed;
    case Mode::Smart:
      updateSmart(state);
      return smartOpen_ ? Target::Open : Target::Closed;
    case Mode::Auto:
    default:
      return Target::Stock;
  }
}

Lockout ValveController::evaluateLockout(const VehicleState &state,
                                         uint32_t nowMs, uint16_t batteryMv) {
  const SafetySettings &sf = settings_.safety;

  if (!settings_.polarityConfirmed) return Lockout::NotCommissioned;

  if (sf.batterySenseFitted &&
      (batteryMv < sf.minBatteryMv || batteryMv > sf.maxBatteryMv)) {
    return Lockout::Voltage;
  }

  if (sf.requireCanForOverride && !state.anyValid) return Lockout::CanLost;

  const bool engineRunning = state.rpmValid && state.rpm >= sf.engineRunningRpm;
  if (sf.requireEngineRunning && !engineRunning) return Lockout::EngineOff;

  if (sf.minCoolantC > -273) {
    if (!state.coolantValid || state.coolantC < sf.minCoolantC) {
      return Lockout::Warmup;
    }
  }

  if (sf.startupQuietMs > 0 && engineWasRunning_ &&
      (nowMs - engineStartMs_) < sf.startupQuietMs) {
    return Lockout::StartupDelay;
  }

  return Lockout::None;
}

ControllerOutput ValveController::applyTarget(Target target,
                                              Lockout lockout) const {
  ControllerOutput out;
  out.target = target;
  out.lockout = lockout;
  switch (target) {
    case Target::Open:
      out.interceptRelay = true;
      out.solenoidDrive = !settings_.energizedClosesValve;
      out.overrideActive = true;
      break;
    case Target::Closed:
      out.interceptRelay = true;
      out.solenoidDrive = settings_.energizedClosesValve;
      out.overrideActive = true;
      break;
    case Target::Stock:
    default:
      out.interceptRelay = false;
      out.solenoidDrive = false;
      out.overrideActive = false;
      break;
  }
  return out;
}

ControllerOutput ValveController::update(uint32_t nowMs, Mode mode,
                                         const VehicleState &state,
                                         uint16_t batteryMv) {
  if (!started_) begin(nowMs);

  if (testActive_) {
    if (static_cast<int32_t>(nowMs - testUntilMs_) >= 0) {
      testActive_ = false;
    } else {
      ControllerOutput out;
      out.target = Target::Stock;
      out.interceptRelay = testIntercept_;
      out.solenoidDrive = testSolenoid_;
      out.overrideActive = testIntercept_;
      out.testActive = true;
      // A test resets the dwell clock so normal control does not immediately
      // slam the flap the other way when the test expires.
      currentTarget_ = Target::Stock;
      lastChangeMs_ = nowMs;
      last_ = out;
      return out;
    }
  }

  // Track engine starts for the post-start quiet window. The first time we see
  // the engine running counts as a start, which means a controller reset
  // mid-drive costs one quiet window rather than locking out forever.
  const bool engineRunning =
      state.rpmValid && state.rpm >= settings_.safety.engineRunningRpm;
  if (engineRunning && !engineWasRunning_) {
    engineStartMs_ = nowMs;
  }
  engineWasRunning_ = engineRunning;

  Target want = desiredTarget(mode, state);
  Lockout lockout = Lockout::None;

  if (want != Target::Stock) {
    lockout = evaluateLockout(state, nowMs, batteryMv);
    if (lockout != Lockout::None) want = Target::Stock;
  }

  if (want != currentTarget_) {
    const bool dwellExpired =
        !dwellArmed_ || (nowMs - lastChangeMs_) >= settings_.safety.minDwellMs;
    // Handing control back to the ECU is a safety action and never waits.
    if (dwellExpired || want == Target::Stock) {
      currentTarget_ = want;
      lastChangeMs_ = nowMs;
      dwellArmed_ = true;
    }
  }

  last_ = applyTarget(currentTarget_, lockout);
  return last_;
}

}  // namespace valve
