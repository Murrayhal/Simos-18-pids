# Commissioning

A freshly flashed controller does nothing. That is deliberate: it does not know
which way your solenoid works, and a controller that guesses will confidently
hold the flap in the wrong position. The LED blinks amber and every override is
refused until you have worked through this page.

Connect a laptop with `pio device monitor` (115200 baud) for all of it.

## Step 1 — check the bus before you touch anything else

```
> can
> sniff on
```

You should see frames scrolling. If `frames decoded into signals` stays at zero
and `sniff` prints nothing:

- wrong crystal value — try `-D CAN_CRYSTAL_MHZ=16`
- CAN-H and CAN-L swapped
- you tapped a bus that is asleep (ignition off) or a different bus
- the 3.3 V wiring problem in [hardware.md](hardware.md)

`sniff off` when you are done.

## Step 2 — find out which way the solenoid works

**This is the step that everything else depends on.** You are determining
whether energising the solenoid opens the flap or closes it.

There is a real reason not to take this from a forum post: the answer depends on
whether the solenoid valve passes or blocks vacuum when energised, *and* on
whether vacuum pulls the flap open or closed on your actuator. Two independent
inversions. Measure it on your car.

Engine off, ignition on. Have someone look at (or put a hand on) the flap
actuator arm on the rear silencer while you run:

```
> test energize 10
```

The relay clicks, the solenoid is energised for ten seconds, and the actuator
arm should move. Then:

```
> test deenergize 10
```

The arm should return. Watch which state corresponds to the flap being **open**
(the actuator rod extended or retracted — follow the linkage, do not guess).

If nothing moves at all:

- no vacuum stored — run the engine for a minute first, then switch off and
  retry immediately, or the reservoir may be empty
- the intercept relay is not actually switching — listen for the click, check
  `RELAY_ACTIVE_LOW`
- MOSFET not turning on — check it is a logic-level part

Now tell the controller what you found:

```
> set polarity closes      # energising the solenoid CLOSES the flap (quiet)
```
or
```
> set polarity opens       # energising the solenoid OPENS the flap (loud)
```

That command is also what marks the controller commissioned, so the amber LED
stops and overrides become possible.

```
> save
```

## Step 3 — teach it the CAN signals

Work through [can-signals.md](can-signals.md). At minimum you need `rpm`. You
also need `coolant` unless you deliberately disable the warm-up interlock with
`set safety.coolant -273`, which is not recommended — a cold flap held open is
just a cold car that is louder.

## Step 4 — first drive

```
> status
```

Check that `lockout` reads `none` with the engine warm and running, and that the
rpm shown matches the tacho.

Then, somewhere you will not annoy anyone:

1. `mode auto` — confirm the car behaves exactly as it did before the install.
2. `mode quiet` — confirm the flap stays shut where it would normally open.
3. `mode open` — confirm it is loud. This is the mode most likely to reveal
   drone at a particular rpm; note where.
4. `mode smart` — drive it. Adjust `smart.openrpm` and `smart.closerpm` to sit
   either side of any drone band you found.

## Step 5 — check for fault codes

After a drive, scan the engine ECU with VCDS, OBDeleven or equivalent.

You are specifically looking for exhaust flap valve (N321) codes — open circuit,
short to plus, short to ground. If one appears, the dummy load is not
convincing the ECU's driver diagnostic. Measure the solenoid coil resistance and
match the resistor more closely.

Clear any codes, drive again, and re-scan. Do this before you decide the install
is finished — a stored DTC will show up on the next service or NCT emissions
readiness check and will be much less obvious then.

## Things worth knowing before you start

- **The flap is a noise device, not an emissions device**, but a permanently
  loud car still has to pass a drive-by noise check and still has to not annoy
  your neighbours at 06:00. `smart.quietkph` exists for exactly that.
- **Anything you splice, you should be able to unsplice.** Use proper crimps and
  leave enough slack that the factory wire can be rejoined.
- **The controller cannot make the flap do anything the actuator cannot.** If
  your vacuum reservoir leaks, or the actuator diaphragm has failed, fix that
  first — this will not paper over it, it will just be a controller wired to a
  broken actuator.
