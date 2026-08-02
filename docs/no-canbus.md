# Controlling the flap directly, without CAN

If you just want to open and close the flap on demand, you do not need the bus
and you may not need the ESP32 either. This page covers the three ways to do it,
cheapest first.

All of them still have to deal with the same two facts:

1. **You have to know which way your solenoid works.** Energised might open the
   flap or close it, depending on whether the valve passes or blocks vacuum and
   which way the actuator pulls. Two independent inversions, so measure it —
   [commissioning.md](commissioning.md) step 2 works for any of these options,
   or use a bench supply and a vacuum source on the bench.
2. **If you disconnect the ECU's driver from a load, it can log a fault.** The
   exhaust flap valve (N321) circuit has an open-circuit diagnostic. Every
   option below except the first leaves a dummy resistor on the ECU's output so
   it still sees something that looks like a solenoid coil.

---

## Option 1 — pull the vacuum hose

Zero cost, zero wiring, fully reversible, and worth knowing about before you
build anything.

Pull the vacuum hose off the flap actuator and cap it. The actuator loses its
vacuum signal and the flap sits wherever its spring puts it — permanently. On
most of these systems that is open, i.e. loud.

The solenoid stays electrically connected, so **no DTC**. The ECU carries on
switching a solenoid that is no longer connected to anything that matters.

What you give up: all control. It is one state, forever, and you get whichever
state the spring gives you. If the spring holds it closed, this option does
nothing for you.

---

## Option 2 — a switch, no microcontroller  ← recommended if you want control

One three-position switch, one relay, one resistor. You get **AUTO / OPEN /
QUIET** with no firmware, nothing to commission in software, and nothing to go
wrong at 3am on a motorway.

Parts:

| Qty | Part |
| --- | --- |
| 1 | DPDT relay, 12 V coil, automotive |
| 1 | DPDT **ON-OFF-ON** toggle switch (centre off) |
| 1 | ~39 Ω 5 W resistor, matched to your solenoid coil |
| 1 | 1N4007 flyback diode |
| 1 | 2 A fuse + holder, switched 12 V feed |

Wiring. The relay is the same intercept described in
[hardware.md](hardware.md) — the switch simply replaces the ESP32's two GPIOs:

```
Switch pole 1 (relay coil):
    COM  ── switched +12V (fused)
    UP   ─┐
          ├── relay coil ── GND       coil energised in UP and DOWN,
    DOWN ─┘                            off in CENTRE

Switch pole 2 (what the solenoid does while we have control):
    COM  ── relay pole A, NO contact  (the solenoid's low side)
    UP   ── GND                        solenoid ENERGISED
    DOWN ── not connected              solenoid DE-ENERGISED

Relay pole A:  COM = solenoid low side
               NC  = ECU driver wire     (factory path)
               NO  = switch pole 2 COM

Relay pole B:  COM = ECU driver wire
               NC  = not connected
               NO  = 39Ω 5W ── GND       (dummy load, only while intercepting)

Flyback diode across the solenoid coil, cathode to +12V.
```

Which gives you:

| Switch | Relay | Result |
| --- | --- | --- |
| CENTRE | off | **AUTO** — solenoid wired straight to the ECU, car is stock |
| UP | on | Solenoid energised, ECU on the dummy load |
| DOWN | on | Solenoid de-energised, ECU on the dummy load |

Whether UP is "loud" or "quiet" depends on your solenoid's polarity. Measure it,
then label the switch accordingly — that is the entire commissioning process for
this option.

Practical notes:

- Mount the relay near the solenoid, in the boot, so only the coil wire and the
  pole-2 wires run to the cabin. Pole 2 carries the solenoid current, around
  half an amp, which any toggle switch handles easily.
- Take the +12 V from a **switched** source. On permanent live, leaving the
  switch in UP parks an energised solenoid on a sleeping car.
- If you cannot find a DPDT centre-off switch, two separate SPST switches do the
  same job: one enables the relay, the other picks the state.

---

## Option 3 — this firmware, with the CAN tap left off

Use the ESP32 build if you want the button-cycles-modes behaviour, the LED codes
and the bench-test command, but you do not want to splice into the powertrain
bus. Build and wire everything in [hardware.md](hardware.md) **except** the
MCP2515 module and the CAN splice, then:

```
> set can off
> set polarity closes        (or opens - measure it first)
> save
```

What changes:

- **SMART disappears.** It has nothing to decide from. The button cycles
  AUTO → OPEN → QUIET → AUTO, and `mode smart` is refused.
- **The bus interlocks are skipped rather than failed.** Warm-up, engine
  running, post-start delay and bus-loss all need data you no longer have, so
  they are bypassed. They are not silently ignored — they cannot be satisfied,
  and leaving them armed would refuse every override forever.
- **Polarity commissioning still applies**, and so does the battery window if
  you fitted the sense divider. The controller still will not move until you
  have told it which way the solenoid works.
- The startup banner says `no CAN tap` instead of nagging you to run `hunt`.

Because there is no engine-running interlock left, **the switched 12 V feed is
now the only thing stopping an energised solenoid draining a parked battery**.
It was good practice before; here it is load-bearing. Do not run this variant
off permanent live.

You can change your mind later: fit the MCP2515, `set can on`, work through
[can-signals.md](can-signals.md), and SMART comes back.

---

## Which one

- **Want it loud all the time and nothing else?** Option 1. Pull the hose, done.
- **Want to choose, and want the factory behaviour still available?** Option 2.
  It does everything most people actually want from this, for about £15, with
  no software in the loop.
- **Want the button/LED interface, or think you might add the bus later?**
  Option 3.

Option 2 is the honest recommendation for a direct-control-only install. The
firmware in this repo earns its keep when it is reading the bus — deciding for
you in SMART, and refusing to open a stone-cold engine. Strip that away and you
have an expensive, more failure-prone switch.
