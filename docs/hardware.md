# Hardware

## What this thing actually does

The flap on each tailpipe is moved by a small **electric servo motor** — three
wires, Audi part 4H0133246J, shared with the Mk7 Golf R and a lot of the rest of
the range. Two of the wires are 12 V and ground. The third is a **PWM signal
from the ECU whose duty cycle commands the position**. The actuator has its own
electronics inside; it takes the commanded position and drives itself there.

That matters for two reasons:

1. **You cannot control it with a switch.** There is no coil to energise. Feed
   it 12 V or ground on the signal pin and you get whatever the actuator's
   firmware does with an invalid command, which is not a position you chose.
2. **We never have to guess what "open" means.** The ECU already sends the right
   commands — it opens the flap in Dynamic and closes it in Comfort. So the
   controller *measures* the ECU's own PWM in each drive mode, stores those two
   numbers, and replays them. What it sends is what the car sends.

So the controller sits on the signal wire and either gets out of the way, or
substitutes its own PWM.

Everything is arranged so "no power" and "controller crashed" both land on
factory behaviour:

- The intercept relay is de-energised at reset, which routes the ECU's signal
  straight through to the actuator.
- The PWM pin is left as a high-impedance input at boot and whenever we are not
  intercepting, so we are never fighting the ECU on a shared wire.
- A 4 second watchdog resets the ESP32 if the main loop stalls, and a reset is
  the same as never having been fitted.
- Pull the fuse and the car is stock.

If you only want to open and close the flap on demand, see
[no-canbus.md](no-canbus.md) — you still need the microcontroller for the PWM,
but you can skip the whole CAN side.

## Before you buy anything: check your actuator

Back-probe the actuator connector with the ignition on.

- **Three pins** — this design. One is ~12 V, one is ground, one carries a
  square wave that changes duty when you switch between Comfort and Dynamic.
- **Two pins** — you have a vacuum solenoid, not a servo. Different car or
  different build; this firmware's output stage does not apply.
- **Four or more pins** — likely a motor with separate position feedback. The
  intercept idea still works but you would also have to synthesise the feedback,
  which this firmware does not do.

Confirm the third wire really is PWM before going further. Once the controller
is built you can do this with `probe`; with a scope, look for a fixed-frequency
square wave whose mark-space ratio changes with drive select.

## Bill of materials

| Qty | Part | Notes |
| --- | ---- | ----- |
| 1 | ESP32 dev board (ESP32-WROOM-32, e.g. DevKitC) | Any board with the pins in `src/board_config.h` broken out |
| 1 | MCP2515 + transceiver CAN module | Omit for a no-CAN build. See the 3.3 V note below |
| 1 | SPDT relay, 12 V coil, automotive | Only one pole needed if you use an off-the-shelf simulator; two if you switch a load yourself |
| 1 | Logic-level N-channel MOSFET, e.g. 2N7002 / BSS138 | Open-drain PWM output. Small signal, not a power part — it switches a signal line, not a motor |
| 1 | 4.7 kΩ pull-up to 12 V | Completes the open-drain output. Set `ACTUATOR_PWM_INVERTED=1` |
| 2 | 47 kΩ and 12 kΩ resistors | Divider on the sense input, 14 V in gives about 2.85 V out |
| 1 | BAT54S or 3.3 V zener | Clamp on the sense input, belt and braces |
| 1 | Exhaust valve simulator, or a resistor | ECU-side load while intercepting — see below, this is the open question |
| 1 | 12 V → 5 V automotive buck converter, ≥1 A | An MP1584 module works; a proper automotive-rated part is better |
| 1 | SMBJ, e.g. SMBJ24A, TVS diode | Across the 12 V input, load-dump protection |
| 1 | Schottky or P-FET reverse polarity protection | On the 12 V input |
| 1 | 2 A blade fuse and holder | In the 12 V feed, close to the tap |
| 1 | Momentary push button | Panel mount, dashboard or centre console |
| 1 | RGB LED (common cathode) + 3 × 330 Ω | Or one plain LED; the blink codes still tell you everything |
| — | Sealed enclosure, automotive connectors, loom tape | It lives in a car |

Note there is no power MOSFET and no flyback diode. We are switching a signal,
not a motor — the actuator's own electronics do the motor driving.

Optional battery sense: 100 kΩ / 22 kΩ divider from switched 12 V to
`PIN_BATTERY_SENSE` with a 100 nF cap to ground. Set `set safety.batsense on`
once fitted.

## The intercept wiring

Cut the **signal** wire between the car's harness and the actuator. The
actuator's 12 V and ground stay on their factory feeds.

```
                              ECU signal wire
                                    │
                    ┌───────────────┴───────────────┐
                    │                               │
              [ COM  pole A ]              47k ─┬─ 12k ─ GND
              /            \                    │
        NC  /                \  NO         PIN_ECU_SENSE
           /                    \          (always connected,
   actuator signal        our PWM output    both relay states)
        pin                     │
                          ┌─────┴─────┐  4.7k to +12V
                          │  2N7002   │
                          └─────┬─────┘
                               GND

              [ COM  pole B ]  ── ECU signal wire
              /            \
        NC  /                \  NO
      (open)              ECU-side load / valve simulator
```

Relay de-energised, the reset state: the ECU's signal goes straight to the
actuator, and we sit on the sense divider watching. The car is stock down to
the wire, and this is also how the controller **learns** — it reads the ECU's
real commands while doing nothing.

Relay energised: the actuator listens to our PWM instead, and the ECU's output
gets whatever load pole B provides.

### The open question: what the ECU-side needs

The ECU notices when the actuator is missing. That is not speculation — several
companies sell "exhaust valve simulators" specifically to stop the fault codes
on cars running a valveless track exhaust, which only makes sense if the ECU is
checking.

What it checks is what I could not establish, and it determines what pole B
needs to be connected to:

- **If the ECU only watches the electrical load** on its output, a resistor of
  roughly the actuator's input impedance is enough.
- **If the actuator reports back** on the same wire, a resistor will not do it
  and you need something that actively responds.

Two ways to settle it on your car:

1. **Measure.** With the controller built, `probe` the line with the actuator
   plugged in, then unplug the actuator and `probe` again. If the waveform is
   identical, nothing is coming back from the actuator and it is one-way PWM.
   If it changes, the actuator is talking and you need a real emulator.
2. **Buy the answer.** Wire a commercially made exhaust valve simulator to
   pole B. They exist precisely to satisfy this diagnostic, and it saves you
   reverse-engineering it.

Either way, scan for codes after the first drive, in the intercepting modes as
well as AUTO. This is the part of the install most likely to need a second go.

### Simpler option: no intercept at all

If you do not care about keeping the factory behaviour, skip the relay. Unplug
the actuator from the car harness, wire our PWM output straight to it, and put
a valve simulator on the harness side permanently. Fewer parts, no relay to
transfer, and the ECU is permanently satisfied — but AUTO is gone and the
controller is now the only thing that can move the flap, so a firmware fault
leaves it wherever it was.

## Signal levels

The sense divider is sized for a 12 V signal. **Check yours first** — if your
actuator's command line runs at 5 V, a 47 k / 12 k divider gives about 1.0 V,
which is still a valid logic high for the ESP32 but leaves less margin. Drop the
top resistor to 10 k for a 5 V signal.

The output stage is open-drain: the MOSFET pulls the line low, the pull-up takes
it high. That inverts the sense of the duty cycle, which is why
`ACTUATOR_PWM_INVERTED` defaults to 1 for this arrangement — the firmware
compensates so the numbers you learn and the numbers you replay mean the same
thing. Pull the pull-up to whatever rail the actuator's input expects, not
automatically to 12 V.

## Power

Take 12 V from a **switched** (ignition) source, not permanent live. Fuse it at
2 A at the tap. Ground to a chassis point, and use the *same* ground reference
for the sense divider and the output MOSFET, otherwise the duty cycle you
measure is not the duty cycle the actuator sees.

Order of protection on the input: fuse → reverse polarity protection → TVS →
buck converter → ESP32.

## Pin map

Defaults are in `firmware/src/board_config.h`. Override them with `build_flags`
in `platformio.ini` rather than editing the header, so your wiring survives a
`git pull`.

| Function | Default GPIO |
| --- | --- |
| MCP2515 CS | 5 |
| MCP2515 INT | 4 |
| MCP2515 SCK / MOSI / MISO | 18 / 23 / 19 (VSPI) |
| Intercept relay | 25 |
| Actuator PWM output | 26 |
| ECU signal sense (input only pin) | 35 |
| Button (to ground, internal pull-up) | 27 |
| LED red / green / blue | 32 / 33 / 14 |
| Battery sense (ADC1, optional) | 34 |

`PIN_ECU_SENSE` needs an interrupt-capable pin. GPIO 35 is input-only, which is
what you want on a line that is also connected to the ECU.

## The MCP2515 3.3 V problem

Skip this section entirely for a no-CAN build.

The common blue MCP2515 modules run the whole board at 5 V with a TJA1050
transceiver. The ESP32 is a 3.3 V part, and two things go wrong:

1. The module's MISO output swings to 5 V and drives it into an ESP32 pin that
   is not 5 V tolerant.
2. The MCP2515's input threshold at 5 V is roughly 0.7 × Vdd = 3.5 V, so the
   ESP32's 3.3 V outputs are marginal — it often works on the bench and fails
   when hot.

Pick one of these, in order of preference:

- **Buy a 3.3 V module.** Some boards ship with an SN65HVD230 or a regulator
  plus level shifter. Simplest correct answer.
- **Convert the common module.** Lift the TJA1050, fit an SN65HVD230 breakout in
  its place, and power the whole board from 3.3 V. Well documented modification.
- **Level shift.** Run the module at 5 V and put a bidirectional level shifter on
  SCK/MOSI/CS/INT and MISO. More parts, more to go wrong.

Do not just wire a 5 V module straight to the ESP32 and hope.

Also check the crystal on your module. 8 MHz is most common, some are 16 MHz.
Wrong value gives you a bus that initialises fine and never receives a frame.
Set it with `-D CAN_CRYSTAL_MHZ=16` in `platformio.ini`.

## CAN tap

Splice onto the 500 kbit/s powertrain CAN pair. **The controller is configured
listen-only** (`MCP_LISTENONLY`) and never transmits — it has no business putting
frames on a bus that also carries braking and steering traffic.

Do not fit a 120 Ω termination resistor. The bus is already terminated at both
ends; adding a third resistor drops the effective termination and can take the
bus down. Many MCP2515 modules have a termination resistor fitted on the board —
check for one and remove it or cut its jumper.

Keep the stub from the splice to the module short, and twist it.
