# S3 8V exhaust valve controller

An ESP32 controller for the factory exhaust flap on an Audi S3 8V. It sits
between the ECU and the flap solenoid, reads engine speed, pedal, road speed,
coolant and drive select off the powertrain CAN bus, and gives you four modes on
a single dashboard button:

| Mode | Behaviour |
| --- | --- |
| **AUTO** | Factory behaviour. The controller electrically removes itself from the circuit. |
| **SMART** | Opens on rpm, pedal or drive select, with hysteresis so it does not chatter. |
| **OPEN** | Loud, whenever it is safe. |
| **QUIET** | Shut, whenever it is safe. |

## How it stays out of trouble

- **Fail-safe by wiring, not by software.** The intercept relay is de-energised
  at reset, which routes the solenoid straight back to the ECU. A crash, a
  brown-out, a blown fuse or a pulled connector all land on stock behaviour.
- **No DTCs.** The second pole of the intercept relay hangs a dummy load on the
  ECU's driver whenever we take over, so its open-circuit diagnostic stays happy.
- **Listen-only on CAN.** The controller never transmits onto a bus that also
  carries braking and steering traffic.
- **Interlocks.** No overrides on a cold engine, a stopped engine, a dead bus,
  or during the first few seconds after start.
- **No guessed calibration.** It ships with the solenoid polarity unknown and
  every CAN signal disabled, refuses to move until you have commissioned it on
  your own car, and includes the tooling to do that.

## Layout

```
firmware/
  lib/valvecore/   portable C++: state machine, CAN decode, button, LED, console
  src/             ESP32 platform layer and pin map
  test/test_core/  host unit tests
docs/
  hardware.md      BOM, the intercept wiring, the MCP2515 3.3 V trap
  commissioning.md what to do on the car, in order
  can-signals.md   finding your car's frame IDs and bit offsets
  no-canbus.md     direct control with no bus tap, including a no-firmware build
  operation.md     modes, LED codes, console reference, drone tuning
```

## Building

```sh
make test                      # host unit tests, no toolchain beyond g++
pio run -e esp32dev -d firmware        # build
pio run -e esp32dev -d firmware -t upload
pio device monitor -d firmware         # console at 115200
```

## Getting started

1. Build the hardware per [docs/hardware.md](docs/hardware.md). The MCP2515
   3.3 V section is the part people get wrong.
2. Flash it, then work through [docs/commissioning.md](docs/commissioning.md).
   Nothing will move until you do — that is the intended behaviour, not a fault.
3. Find your CAN signals with [docs/can-signals.md](docs/can-signals.md).
4. Day-to-day use and tuning is in [docs/operation.md](docs/operation.md).

**Just want a switch that opens and closes it?** You may not need any of this —
see [docs/no-canbus.md](docs/no-canbus.md), which covers direct control with one
switch and a relay and no microcontroller at all.

Scan for fault codes after the first drive, and keep a copy of `show` output —
it is the only record of your commissioning.

## Scope

This controls the noise flap. It is not an emissions device and this does not
touch emissions equipment, but a louder car still has to pass a drive-by noise
check and still has neighbours. `smart.quietkph` and the warm-up interlock exist
for that reason.
