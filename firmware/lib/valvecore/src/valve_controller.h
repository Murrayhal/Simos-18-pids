// The decision layer: mode + vehicle state -> what the two output pins do.
#pragma once

#include "settings.h"
#include "valve_types.h"

namespace valve {

struct ControllerOutput {
  // What the flap should be doing.
  Target target = Target::Stock;
  // Intercept relay: de-energised hands the actuator back to the ECU, so every
  // failure path (reset, brown-out, blown fuse, firmware crash) lands on stock
  // behaviour without anybody having to decide that it should.
  bool interceptRelay = false;
  // Duty cycle to command the actuator with. Only meaningful while
  // intercepting; the platform layer stops generating PWM entirely otherwise.
  DutyTenths commandDuty = 0;
  // Set when an override was asked for but refused, and why.
  Lockout lockout = Lockout::None;
  // True when the controller is actually holding the flap somewhere the ECU
  // did not put it.
  bool overrideActive = false;
  // True while a bench test is driving the outputs directly.
  bool testActive = false;
};

class ValveController {
 public:
  explicit ValveController(const Settings &settings);

  void begin(uint32_t nowMs);

  // Call every loop. batteryMv is ignored unless battery sense is fitted.
  ControllerOutput update(uint32_t nowMs, Mode mode, const VehicleState &state,
                          uint16_t batteryMv);

  // Commissioning aid: drive the actuator at a chosen duty for a bounded time,
  // ignoring every interlock. This is how the installer finds the end
  // positions, so it deliberately works before anything is commissioned.
  // Callers are expected to refuse it with the engine running.
  void startTest(bool interceptRelay, DutyTenths duty, uint32_t durationMs,
                 uint32_t nowMs);
  void stopTest();
  bool testActive() const { return testActive_; }

  const ControllerOutput &last() const { return last_; }
  bool smartWantsOpen() const { return smartOpen_; }

 private:
  Target desiredTarget(Mode mode, const VehicleState &state);
  void updateSmart(const VehicleState &state);
  Lockout evaluateLockout(const VehicleState &state, uint32_t nowMs,
                          uint16_t batteryMv);
  ControllerOutput applyTarget(Target target, Lockout lockout) const;

  const Settings &settings_;

  Target currentTarget_;
  uint32_t lastChangeMs_;
  // The dwell timer only exists to stop the flap hammering around a threshold.
  // It has no business delaying the first move after a reset, which could
  // otherwise leave a mid-drive restart ignoring the driver.
  bool dwellArmed_;
  bool smartOpen_;

  bool engineWasRunning_;
  uint32_t engineStartMs_;

  bool testActive_;
  bool testIntercept_;
  DutyTenths testDuty_;
  uint32_t testUntilMs_;

  ControllerOutput last_;
  bool started_;
};

}  // namespace valve
