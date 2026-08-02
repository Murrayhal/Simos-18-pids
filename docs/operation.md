# Operating it

## The button

One momentary button does everything.

| Press | Action |
| --- | --- |
| Short | Cycle mode: AUTO → SMART → OPEN → QUIET → AUTO |
| Long (1.2 s) | Save the current mode as the power-on default. Three white flashes confirm. |

A button that is already held down when the controller powers up is ignored
until you let go, and one held for 30 seconds is assumed to be a shorted wire
and stops generating events. Neither case can cycle modes on you.

## The LED

| Pattern | Meaning |
| --- | --- |
| Green, steady | AUTO — factory behaviour, controller stood down |
| Blue, steady | SMART — deciding from bus data |
| Red, steady | OPEN — forced open |
| Cyan, steady | QUIET — forced closed |
| Mode colour, 2 Hz blink | Override refused. Run `status` for the reason. |
| Amber, 5 Hz blink | Not commissioned. Nothing will move. See [commissioning.md](commissioning.md). |
| White, 5 Hz blink | Bench test driving the outputs |
| Three white flashes | Default mode saved |

With a single-colour LED you lose the colour but keep every blink code, which is
the part that matters when something is wrong.

## The modes

With `can` off there is no SMART: the button cycles AUTO → OPEN → QUIET, and
the bus interlocks are skipped. See [no-canbus.md](no-canbus.md).

**AUTO** de-energises the intercept relay entirely. The ECU is wired to the
solenoid exactly as it was from the factory. This is the power-on default until
you change it.

**SMART** opens the flap when any of these is true, subject to the interlocks:

- engine speed at or above `smart.openrpm`
- accelerator at or above `smart.openpedal`
- drive select is in dynamic, if `smart.drive` is on

and closes it again when engine speed is at or below `smart.closerpm` **and**
pedal is at or below `smart.closepedal`. Between the two pairs of thresholds it
holds whatever it was doing, which is what stops the flap chattering as you sit
at a steady throttle right on a threshold.

If `smart.quietkph` is non-zero, the flap is held shut below that road speed —
unless the pedal is past `smart.openpedal`, so pulling away hard still opens it
but crawling out of an estate does not.

**OPEN** and **QUIET** force the flap one way, subject to the interlocks.

## The interlocks

An override is refused, and the flap handed back to the ECU, whenever:

| Lockout | Condition |
| --- | --- |
| `not-commissioned` | Solenoid polarity has never been confirmed |
| `voltage` | Battery outside `safety.minmv`..`safety.maxmv` (only if battery sense is fitted) |
| `can-lost` | No usable bus data, and `safety.requirecan` is on |
| `engine-off` | Engine speed below `safety.runrpm`, and `safety.requireengine` is on |
| `warmup` | Coolant below `safety.coolant`, or coolant unknown |
| `startup-delay` | Within `safety.startupquiet` ms of the engine starting |

Handing control back is immediate and never waits for the dwell timer. Taking
control does wait, so the flap cannot hammer around a threshold.

## Console reference

Serial at 115200. `pio device monitor` from the `firmware` directory.

```
status                      current mode, outputs, decoded signals
mode [auto|smart|open|quiet]
show                        dump settings as pasteable commands
set <key> <value>
sig <name> <id> <startbit> <len> <le|be> <scale> <offset>
sig <name> [off]            show or disable one signal
test energize|deenergize|stock [seconds]   engine off only
hunt start|mark <value>|top [n]|stop
sniff on [id]|off           dump raw frames, rate limited
can                         bus statistics
save                        persist settings to NVS
defaults                    restore factory settings (not saved until you save)
```

Settings are only written to flash on `save`, or on a long button press. A power
cycle before that loses them.

### Setting keys

| Key | Range | Default | What it does |
| --- | --- | --- | --- |
| `polarity` | `closes` \| `opens` | — | Declares which way the solenoid works, and commissions the controller |
| `can` | on/off | on | A CAN tap is fitted. Off disables SMART and skips the bus interlocks — see [no-canbus.md](no-canbus.md) |
| `confirm` | on/off | off | Manually un-commission it again |
| `default` | mode | `auto` | Mode selected at power-on |
| `smart.openrpm` | 0–9000 | 3200 | Open at or above this |
| `smart.closerpm` | 0–9000 | 2600 | Close at or below this |
| `smart.openpedal` | 0–100 | 60 | Open at or above this pedal % |
| `smart.closepedal` | 0–100 | 40 | Close at or below this pedal % |
| `smart.quietkph` | 0–300 | 0 | Hold shut below this speed off-throttle. 0 disables |
| `smart.drive` | on/off | off | Follow drive select |
| `smart.dynraw` | 0–255 | 0 | Raw drive select value meaning dynamic |
| `safety.coolant` | -273–200 | 60 | Minimum coolant °C. -273 disables the check |
| `safety.timeout` | 50–60000 | 500 | Signal staleness timeout, ms |
| `safety.requirecan` | on/off | on | Refuse overrides with no bus data |
| `safety.requireengine` | on/off | on | Refuse overrides with the engine stopped |
| `safety.runrpm` | 0–3000 | 400 | Engine counts as running above this |
| `safety.startupquiet` | 0–60000 | 5000 | Stay stock for this long after start, ms |
| `safety.dwell` | 0–10000 | 750 | Minimum time between flap changes, ms |
| `safety.batsense` | on/off | off | Battery sense divider is fitted |
| `safety.minmv` | 6000–16000 | 11000 | Lower battery limit, mV |
| `safety.maxmv` | 6000–20000 | 15500 | Upper battery limit, mV |

## Tuning out drone

Drone is a resonance at a specific engine speed under light load, usually
somewhere between 1800 and 2800 rpm in a high gear. Find where yours is by
driving in OPEN and noting the rpm where the cabin starts to boom.

Then set `smart.closerpm` above the top of the drone band and `smart.openrpm`
above that again. The flap will be shut through the drone band on part throttle,
and the pedal threshold still gets you the noise when you actually ask for it.

Example, for drone from 1900 to 2600:

```
> set smart.closerpm 2700
> set smart.openrpm 3200
> set smart.openpedal 55
> save
```

## Troubleshooting

**LED blinks amber.** Not commissioned. `set polarity closes|opens`.

**LED blinks the mode colour.** An interlock is refusing the override. `status`
names it.

**Modes change but nothing happens to the exhaust.** Either the polarity is
declared backwards — swap it and see if the modes swap over — or the relay is
not switching. `test energize 10` with the engine off and listen for the click.

**`status` shows rpm that does not match the tacho.** Wrong hunt candidate.
Re-run the hunt and take the next result down.

**Everything works, then stops after a few minutes.** Look at the MCP2515 3.3 V
note in [hardware.md](hardware.md). Marginal logic levels typically work cold
and fail warm.

**Engine light after fitting.** Scan it. An N321 circuit code means the dummy
load is not satisfying the ECU's driver diagnostic — measure the solenoid coil
and match the resistor value more closely.
