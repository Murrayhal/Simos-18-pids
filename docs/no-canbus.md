# Controlling the flap directly, without CAN

You do not need the bus to open and close the flap on demand. You do, however,
need the microcontroller.

## Why a switch is not enough

The actuator is a servo motor with its own electronics, commanded by a **PWM
signal whose duty cycle is the requested position**. There is no coil to
energise. A toggle switch can only give it a steady high or a steady low, which
is not a position — it is an invalid command, and what the actuator does with
one is its business, not yours.

So every option below still involves generating PWM. What you can skip is the
MCP2515, the CAN splice, and everything that reads the bus.

## Option 1 — this firmware, no CAN tap  ← recommended

Build everything in [hardware.md](hardware.md) **except** the MCP2515 module and
the CAN splice. Then:

```
> set can off
> probe                  (confirm there is PWM on the signal line)
> learn closed           (in Comfort)
> learn open             (in Dynamic)
> save
```

You get AUTO / OPEN / QUIET on the button, the LED codes, and the bench test
commands. What changes:

- **SMART disappears.** It has nothing to decide from. The button cycles
  AUTO → OPEN → QUIET → AUTO, and `mode smart` is refused.
- **The bus interlocks are skipped rather than failed.** Warm-up, engine
  running, post-start delay and bus-loss all need data you no longer have, so
  they are bypassed. They are not silently ignored — they cannot be satisfied,
  and leaving them armed would refuse every override forever.
- **Learning still works.** It reads the ECU's PWM off the signal wire, which
  has nothing to do with CAN.
- **Actuator commissioning still applies**, and so does the battery window if
  you fitted the sense divider.

Because there is no engine-running interlock left, **the switched 12 V feed is
the only thing stopping the controller sitting awake on a parked car**. It was
good practice before; here it is load-bearing. Do not run this variant off
permanent live.

You can change your mind later: fit the MCP2515, `set can on`, work through
[can-signals.md](can-signals.md), and SMART comes back.

## Option 2 — no intercept either

If the factory behaviour is not worth keeping, drop the relay as well. Unplug
the actuator from the car's harness, wire the controller's PWM output straight
to it, and put an exhaust valve simulator on the harness side permanently so the
ECU stays happy.

Fewer parts and no relay transfer to get right. The trade is that AUTO is gone —
the controller is now the only thing that can move the flap, so a firmware fault
or a dead controller leaves it wherever it last was rather than handing it back.

Set `set default open` or `set default quiet` so a power cycle lands somewhere
you chose.

## Option 3 — mechanical

Worth knowing about, though it is cruder than it was on the vacuum cars.

The flap is driven through a linkage from the servo. You can disconnect or pin
the linkage so the flap is mechanically held open regardless of what the
actuator does. Cheap, reversible if you keep the parts, and completely
independent of any electronics.

The ECU will carry on commanding an actuator that is no longer connected to
anything, so no fault code — but you also get no control, and the actuator will
be driving against a stop, which is not what it was designed to do. If you go
this way, disconnect the linkage properly rather than jamming the flap.

## Which one

- **Want to choose, day to day?** Option 1. It is the whole controller minus the
  bus, and the parts you skip are the fiddly ones.
- **Never going to use AUTO?** Option 2 removes the relay and the ECU-side load
  question in one go, at the cost of the fail-safe.
- **Just want it loud and never think about it again?** Option 3, or honestly, a
  different exhaust.

Unlike the vacuum systems on older cars, there is no zero-cost electrical trick
here. Unplugging the actuator sets a code and leaves the flap wherever it
happened to stop.
