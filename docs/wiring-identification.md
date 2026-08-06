# Identifying the actuator wiring on your car

Wire colours and pin numbering vary by model year, market and build, so the only
reliable pinout is the one you measure. This is a multimeter procedure — no
scope needed, though one makes step 4 quicker.

If you have the factory wiring diagram (erWin, a workshop manual, an ODIS
extract), that gives you pin numbers, wire colours and the ECU pin it lands on
directly. Measure anyway before you cut: a diagram for the wrong build year
looks exactly as authoritative as the right one.

## What you need

- Multimeter. One with a **duty cycle (%) and frequency (Hz)** function makes
  this much easier, but plain DC volts is enough.
- Back-probe pins or fine probes. **Do not pierce the insulation** — a pierced
  automotive wire corrodes from the inside and fails in two years.
- Somewhere to write down what you find.

Take a photo of the connector, both halves, before you disturb anything.

## Step 1 — find the ground pin

Ignition **off**, key out.

Measure resistance from each of the three pins to a bare chassis point.

| Reading | Meaning |
| --- | --- |
| Under a few ohms | **Ground.** |
| Open / very high | 12 V feed or signal |

Label it. If two pins read low to chassis, stop — you are probably on the wrong
connector, or something is already damaged.

## Step 2 — find the 12 V feed

Ignition **on**, engine off. Measure DC volts from each remaining pin to the
ground pin you just found.

| Reading | Meaning |
| --- | --- |
| Steady 12–14 V, does not move | **12 V feed.** |
| Something in between, and it *moves* when you change drive mode | **Signal.** |

The third pin is the signal by elimination, but the moving reading is the real
confirmation — see the next step.

## Step 3 — confirm the signal is PWM, and read it

This is the important one, because a plain DC voltmeter reads the **average** of
a PWM waveform. That average is the duty cycle times the rail voltage, so an
ordinary multimeter tells you the duty:

```
duty % ≈ (measured average / rail voltage) × 100
```

Switch drive select between Comfort and Dynamic and watch the reading. On a 12 V
signal you should see something like:

| Drive mode | Average volts | Implied duty |
| --- | --- | --- |
| Comfort (flap shut) | ~1.8 V | ~15 % |
| Dynamic (flap open) | ~9.6 V | ~80 % |

The exact numbers are yours to record — those are illustrative. What matters is
that **the reading changes with drive mode and sits between the rails**. That is
the signature of a PWM position command.

If your meter has duty cycle and frequency functions, use them instead and read
the numbers directly. Note the frequency; you will need it as
`set actuator.pwmhz`.

### Which rail?

Set the meter to min/max hold, or just note the highest average you can provoke
at full open. If the maximum you ever see approaches 12 V, it is a 12 V signal
and the divider values in [hardware.md](hardware.md) are right. If it tops out
near 5 V, drop the divider's top resistor to 10 kΩ and pull the output stage's
pull-up to 5 V, not 12 V.

## Step 4 — rule out a data protocol

**A moving average is PWM. A stubbornly constant average with a flap that still
moves is not**, and that changes the whole design.

Some VAG actuators are LIN slaves: 12 V, ground, and a single-wire data bus.
LIN idles high and sends short frames, so:

| Symptom | Likely |
| --- | --- |
| Average voltage changes with drive mode | PWM. This firmware applies. |
| Average sits near the rail (say 11+ V of 12) and barely moves, but the flap does move | LIN or another data protocol. This firmware's output stage does **not** apply. |
| Meter's duty function reads 95–100 % regardless of drive mode | Same — data, not PWM |

With the controller built, `probe` will tell you the same thing: a signal it can
measure but never calls steady, across many thousands of edges, is a hint that
the line is carrying data rather than a fixed-frequency command. `probe` says so
explicitly in that case.

If it turns out to be LIN, stop and say so — intercepting it means emulating a
LIN slave toward the ECU and mastering the bus toward the actuator, which is a
different piece of work than what is in this repo.

## Step 5 — work out which duty is which position

Get the flap into each state and confirm by eye or ear at the tailpipe, then
write down which average voltage or duty went with which. Do not infer it from
"higher must mean open" — there is no rule that says so.

## Step 6 — one output or two?

Cars with a flap on each tailpipe have two actuators. Check whether they share
the ECU output: with the ignition off and both connectors unplugged from the
harness side, measure resistance between the two signal pins on the *harness*
side.

| Reading | Meaning |
| --- | --- |
| Near zero | One ECU output feeds both. Intercept once, wire your PWM output to both actuators. |
| Open | Two separate outputs. You need two intercept relays, both driven from the same GPIO, and your one PWM output feeding both actuators. |

## Step 7 — does the actuator talk back?

This decides what the ECU-side of the intercept needs, which is the one genuinely
open question in this design.

With the ignition on, measure the signal line average with the actuator
connected. Then unplug the actuator and measure again at the same drive mode.

| Result | Meaning |
| --- | --- |
| Identical | The ECU is just driving a line. A resistive load on the ECU side is likely enough while intercepting. |
| Different | The actuator is loading or driving that wire. A resistor will not satisfy the ECU; you need a proper valve simulator. |

Either way, check for fault codes afterwards. See
[hardware.md](hardware.md#the-open-question-what-the-ecu-side-needs).

## Record sheet

Fill this in and keep it with the car. It is the only record of your specific
wiring, and it is what you will want in two years when something misbehaves.

```
Connector photo:        ____________________
Actuator part number:   ____________________

Pin 1  colour ________  function ____________
Pin 2  colour ________  function ____________
Pin 3  colour ________  function ____________

Signal rail:            ______ V
Carrier frequency:      ______ Hz
Closed (Comfort):       ______ % duty   (______ V average)
Open (Dynamic):         ______ % duty   (______ V average)

Shared output for both actuators?   yes / no
Reading changes with actuator unplugged?   yes / no
```

Then enter it:

```
> set actuator.pwmhz <frequency>
> set actuator.closedduty <closed %>
> set actuator.openduty <open %>
> save
> show
```

Keep the `show` output somewhere. It is the whole configuration in a form you
can paste back in after a reflash.
