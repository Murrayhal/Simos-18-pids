# Finding the CAN signals

## Why there are no defaults in this repo

This firmware ships with every CAN signal **disabled**. There are no
factory-supplied frame IDs or bit offsets, and that is on purpose.

Bit placements on the MQB powertrain bus vary with model year, market and
build. A number copied from a forum post for a different car decodes into
something — it just decodes into the wrong thing, quietly, and you find out when
the controller thinks you are at 6000 rpm on the motorway. Rather than ship
somebody else's numbers, the firmware includes a tool that finds yours.

If you already have a verified DBC for your car, skip to
[Entering signals by hand](#entering-signals-by-hand).

## The hunter

`hunt` watches every frame on the bus and keeps every plausible 8- and 16-bit
field as a candidate — each byte on its own, and each adjacent byte pair in both
byte orders. You then tell it the true value at three or four different, steady
operating points. It least-squares every candidate against what you declared and
reports the ones that fit, along with the scale and offset to use.

A field that never changes cannot fit, a field that changes for other reasons
fits badly, and the one that actually is the signal fits with essentially zero
error. In practice the right answer is the top result and the runners-up are
obviously wrong.

### Finding engine RPM

Engine warm and idling, in park/neutral, handbrake on. Do this with a second
person or with the car stationary — you are reading a laptop while operating the
throttle.

```
> hunt start
```

Hold a steady idle, read the tacho, and mark it:

```
> hunt mark 800
```

Hold a steady 2000 rpm, and mark it:

```
> hunt mark 2000
```

Hold a steady 3500 rpm, and mark it:

```
> hunt mark 3500
```

Then:

```
> hunt top
best fits (paste the sig line for the one that makes sense):
  err     0.31   sig <name> 0x121 16 16 le 0.25 0
  err    22.40   sig <name> 0x121 24 8 le 16 0
  ...
```

The first line has essentially no error and a sensible scale. Commit it:

```
> sig rpm 0x121 16 16 le 0.25 0
> save
> status
```

`status` should now track the tacho as you blip the throttle. If it does not,
you picked the wrong candidate — try the next one down.

The tacho itself is filtered and lags a little, so do not expect the marks to be
exact. Errors of a few rpm are fine; the point is to separate the real signal
from fields that are off by hundreds.

### Finding the other signals

Same procedure, different quantity. `hunt start` resets everything, so do one
signal at a time.

| Signal | How to get three known points |
| --- | --- |
| `pedal` | Hold the pedal at rest (0), roughly half (50), and flat to the floor (100). Approximate is fine — you are looking for a field that is 0 at rest and full scale at the stop. Engine off, ignition on, works for this one. |
| `speed` | Needs a drive, a passenger, and somewhere safe. Steady 30, 60 and 100 km/h against the speedometer. Bear in mind the speedometer over-reads, so the fit will show an offset. |
| `coolant` | Slow. Mark from cold at start-up, again part way through warm-up, and again fully warm, reading the temperature from VCDS or the MFD rather than the dash gauge, which is damped. |
| `drive` | Not a continuous quantity, so the hunter is the wrong tool. Use `sniff` instead — see below. |

### Drive select

Drive select is an enumeration, not a number to fit a line through. Find it by
watching frames change as you switch modes:

```
> sniff on
```

Change the drive select mode on the MMI and watch which byte changes. Narrow it
down with `sniff on 0x<id>` once you have a candidate frame, note the byte
offset, then:

```
> sig drive 0x<id> <startbit> 8 le 1 0
> save
```

Now put the car in dynamic and read the raw value off `status`:

```
> status
drive     3
```

Then tell the smart mode about it:

```
> set smart.dynraw 3
> set smart.drive on
> save
```

## Entering signals by hand

```
sig <name> <id> <startbit> <len> <le|be> <scale> <offset>
```

- `name` — one of `rpm`, `pedal`, `speed`, `coolant`, `drive`
- `id` — frame id, decimal or `0x` hex
- `startbit` — DBC bit numbering: bit 0 is the least significant bit of byte 0,
  bit 8 the least significant bit of byte 1. For big-endian signals this is the
  most significant bit of the value, as in a DBC file.
- `len` — 1 to 32 bits
- `le` / `be` — Intel or Motorola byte order
- `scale`, `offset` — `value = raw × scale + offset`

`sig <name>` on its own shows the current setting. `sig <name> off` disables it.
`show` dumps every setting as commands you can paste back in — keep a copy
somewhere, it is the only record of your commissioning.

## What happens when a signal goes missing

Each signal ages out independently after `safety.timeout` milliseconds (500 by
default) and is then treated as invalid. The rules are:

- An invalid signal can never *open* the flap. Missing data does not get you
  noise.
- An invalid signal does not block *closing* it.
- With `safety.requirecan on` (the default), losing the whole bus refuses every
  override and hands the flap straight back to the ECU.
- `rpm` going invalid means the engine does not count as running, which by
  itself refuses every override.

You can watch this on the console: `status` prefixes stale values with
`(stale)`.
