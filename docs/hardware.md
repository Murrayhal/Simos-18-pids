# Hardware

## What this thing actually does

The factory system is: ECU → solenoid valve → vacuum actuator → exhaust flap.
The ECU decides when the flap opens. This controller sits in the wire between
the ECU and the solenoid and can either get out of the way (the ECU keeps
control, exactly as from the factory) or take over and drive the solenoid
itself.

The important design decision is what happens when the ECU is *not* driving the
solenoid and we are. If you simply cut the ECU's output wire, the ECU sees an
open circuit on its driver and logs a fault — on this family that shows up as an
exhaust flap valve N321 open-circuit or short DTC, with the engine light to go
with it. So the intercept uses a **double-pole** relay: one pole moves the
solenoid over to our MOSFET, the other pole simultaneously hangs a dummy load on
the ECU's driver so the ECU still sees something that looks like a solenoid coil.

Everything is arranged so that "no power" and "controller crashed" both land on
factory behaviour:

- The relay coil is de-energised at reset, which routes the solenoid straight
  back to the ECU.
- Both output GPIOs are driven LOW as the very first thing in `setup()`.
- A 4 second watchdog resets the ESP32 if the main loop stalls, and a reset is
  the same as never having been fitted.
- Pull the fuse and the car is stock.

## Bill of materials

| Qty | Part | Notes |
| --- | ---- | ----- |
| 1 | ESP32 dev board (ESP32-WROOM-32, e.g. DevKitC) | Any board with the pins in `src/board_config.h` broken out |
| 1 | MCP2515 + transceiver CAN module | See the 3.3 V note below — this is the one part that needs thought |
| 1 | Automotive DPDT relay, 12 V coil, or two SPDT | Or a 2-channel relay module, driven in lockstep |
| 1 | Logic-level N-channel MOSFET, e.g. IRLZ44N / IRLB8721 | Drives the solenoid low side. Must be logic level: a plain IRF540 will not turn on from 3.3 V |
| 1 | 1N4007 or similar flyback diode | Across the solenoid, cathode to +12 V |
| 1 | Resistor, ~39 Ω 5 W wirewound | Dummy load for the ECU driver. Match it to your measured coil resistance |
| 1 | 12 V → 5 V automotive buck converter, ≥1 A | An MP1584 module works; a proper automotive-rated part is better |
| 1 | SMBJ, e.g. SMBJ24A, TVS diode | Across the 12 V input, load-dump protection |
| 1 | Schottky or P-FET reverse polarity protection | On the 12 V input |
| 1 | 2 A blade fuse and holder | In the 12 V feed, close to the tap |
| 1 | Momentary push button | Panel mount, dashboard or centre console |
| 1 | RGB LED (common cathode) + 3 × 330 Ω | Or one plain LED; the blink codes still tell you everything |
| 1 | 10 kΩ gate pulldown, 100 Ω gate series resistor | Keeps the MOSFET off while the ESP32 boots |
| — | Sealed enclosure, automotive connectors, loom tape | It lives in a car |

Optional battery sense: 100 kΩ / 22 kΩ divider from switched 12 V to
`PIN_BATTERY_SENSE` with a 100 nF cap to ground. Set `set safety.batsense on`
once fitted.

## The MCP2515 3.3 V problem

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

## The intercept wiring

Find the wire between the ECU (or its harness) and the exhaust flap solenoid,
and cut it. The solenoid's other pin stays on its factory +12 V feed.

```
                       +12V (factory feed)
                              │
                        ┌─────┴─────┐
                        │ solenoid  │        1N4007
                        │   coil    │◄──┐  (cathode to +12V)
                        └─────┬─────┘   │
                              │  ───────┘
                          [ COM  pole A ]
                          /            \
                    NC  /                \  NO
                       /                    \
            ECU driver wire            MOSFET drain
            (factory path)                  │
                                          MOSFET
                                            │
                                           GND

                          [ COM  pole B ]  ── ECU driver wire
                          /            \
                    NC  /                \  NO
                  (open)                  39Ω 5W ── GND
```

Relay de-energised (the reset state): pole A connects the solenoid to the ECU,
pole B leaves the dummy load disconnected. The car is stock down to the wire.

Relay energised: pole A moves the solenoid onto our MOSFET, pole B connects the
ECU's driver output to the dummy resistor so it still sees a load.

Two SPDT relays wired in parallel on the coil do exactly the same job as one
DPDT, and 2-channel relay modules are easy to find. Drive both coils from
`PIN_RELAY`.

Sizing the dummy resistor: measure your solenoid's coil resistance with a
multimeter across its two pins, disconnected. Pick a resistor within about 20%
of that, rated for `12² / R` watts with margin — around 3.7 W for a 39 Ω part,
hence the 5 W rating. If the ECU's diagnostic is happy with a wider range you can
be less fussy, but there is no way to know that without trying it.

`RELAY_SETTLE_MS` (20 ms by default) stops the firmware driving the MOSFET while
the relay contacts are still in mid-transfer. Taking control goes relay-then-
MOSFET; handing back goes MOSFET-then-relay.

## Power

Take 12 V from a **switched** (ignition) source, not permanent live, so the
controller cannot sit awake on a parked car. Fuse it at 2 A at the tap. Ground to
a chassis point, and use the *same* ground reference for the MOSFET source.

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
| Solenoid MOSFET gate | 26 |
| Button (to ground, internal pull-up) | 27 |
| LED red / green / blue | 32 / 33 / 14 |
| Battery sense (ADC1, optional) | 34 |

## CAN tap

Splice onto the 500 kbit/s powertrain CAN pair. **The controller is configured
listen-only** (`MCP_LISTENONLY`) and never transmits — it has no business putting
frames on a bus that also carries braking and steering traffic.

Do not fit a 120 Ω termination resistor. The bus is already terminated at both
ends; adding a third resistor drops the effective termination and can take the
bus down. Many MCP2515 modules have a termination resistor fitted on the board —
check for one and remove it or cut its jumper.

Keep the stub from the splice to the module short, and twist it.
