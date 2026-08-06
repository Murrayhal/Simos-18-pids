# Commissioning

A freshly flashed controller does nothing. That is deliberate: it does not know
what commands your actuator expects, and a controller that guesses will
confidently drive the flap somewhere you did not ask for. The LED blinks amber
and every override is refused until you have worked through this page.

Connect a laptop with `pio device monitor` (115200 baud) for all of it.

If you have not yet identified which of the three actuator wires is which, do
[wiring-identification.md](wiring-identification.md) first.

## Step 1 — learn the actuator's commands

**This is the step everything else depends on.** You are capturing the two PWM
commands the ECU already sends for open and closed, so the controller replays
real numbers rather than invented ones.

Ignition on, engine can be off. First check there is anything to read:

```
> probe
200 Hz, duty 15.3 %, 4182 edges, steady
```

If it says `no edges on the signal line at all`, the sense input is on the wrong
wire or the ECU is asleep. Back-probe the actuator connector: of the three pins,
one sits at ~12 V, one at ground, and one carries the square wave. That last one
is the signal.

If it says the line is `parked high or low`, the ECU is not commanding anything
right now. Cycle the ignition or switch drive modes and re-read.

Now capture each end position. Put the car in the drive mode that **closes** the
flap — Comfort or Efficiency — let it settle, and:

```
> learn closed
learned closed = 15.3 % at 200 Hz
still need the open position.
```

Switch to Dynamic, which **opens** it, let it settle, and:

```
> learn open
learned open = 80.1 % at 200 Hz
both positions known, overrides enabled. `save` to keep them.
> save
```

`learn` refuses a signal that is still moving, so if the flap is mid-travel it
will tell you to wait rather than capture a meaningless intermediate value. It
also catches the case where both captures come out identical, which means one
of them was taken in the wrong drive mode.

If your car does not visibly change drive modes, or you cannot tell which
position is which, verify by ear: get the flap into each state and listen at the
tailpipe before you decide which capture is which. Getting them swapped inverts
OPEN and QUIET, which is obvious on the first drive and fixed by re-running the
two `learn` commands.

### Sweeping by hand

If you would rather find the end positions yourself — or your car only ever
uses part of the range — drive the actuator directly with the engine off:

```
> set actuator.pwmhz 200
> test duty 20 10
> test duty 50 10
> test duty 80 10
```

Each command energises the intercept relay and drives that duty for ten
seconds. Watch the flap. When you have found the two positions you want:

```
> set actuator.closedduty 15
> set actuator.openduty 80
> save
```

`test stock` clears an active test early. It is refused with the engine running,
because fighting the ECU on a shared line tells you nothing.

### Check the ECU is still happy

While you are here, settle the question in
[hardware.md](hardware.md#the-open-question-what-the-ecu-side-needs): `probe`
with the actuator connected, then unplug the actuator and `probe` again. If the
numbers are identical, nothing is coming back from the actuator. If they differ,
the actuator is talking on that wire and your ECU-side load has to do more than
be a resistor.

## Step 2 — check the bus

Skip this for a no-CAN build.

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

You are looking for exhaust flap actuator codes. If one appears, the ECU-side
load is not convincing the diagnostic while we are intercepting — see the open
question in [hardware.md](hardware.md#the-open-question-what-the-ecu-side-needs).

A useful discriminator: does the code appear only after a drive in which you
used OPEN or QUIET, and never after a drive spent entirely in AUTO? That points
squarely at the intercept rather than at anything else you changed.

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
  the servo is seized or the flap spindle is coked up, fix that first — this
  will not paper over it, it will just be a controller wired to a broken
  actuator.
- **There are two actuators**, one per tailpipe, on cars fitted with both. They
  are usually driven from the same ECU output; if yours are on separate wires
  you need to intercept both, wired in parallel off the one PWM output.
