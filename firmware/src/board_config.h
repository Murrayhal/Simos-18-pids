// Pin map and board-level constants. Override any of these with -D flags in
// platformio.ini rather than editing the file, so your wiring survives a pull.
#pragma once

// ---------------------------------------------------------------- MCP2515 ---
#ifndef PIN_CAN_CS
#define PIN_CAN_CS 5
#endif
#ifndef PIN_CAN_INT
#define PIN_CAN_INT 4
#endif
// Crystal fitted to your MCP2515 module. The blue "niren" boards are usually
// 8 MHz; some are 16 MHz. Getting this wrong gives you a silent bus.
#ifndef CAN_CRYSTAL_MHZ
#define CAN_CRYSTAL_MHZ 8
#endif
// MQB powertrain CAN runs at 500 kbit/s.
#ifndef CAN_BITRATE_KBPS
#define CAN_BITRATE_KBPS 500
#endif

// ----------------------------------------------------------------- outputs ---
// Intercept relay coil driver. LOW at reset means the ECU keeps control, which
// is exactly what we want on every unplanned restart.
#ifndef PIN_RELAY
#define PIN_RELAY 25
#endif
// Low-side MOSFET gate driving the solenoid while we are intercepting.
#ifndef PIN_SOLENOID
#define PIN_SOLENOID 26
#endif
// Set to 1 if your relay board is active-low (most opto-isolated modules are).
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif
// Time allowed for the relay contacts to finish transferring before the
// solenoid driver is allowed to switch. Stops us driving into a contact that
// is still in mid-air, and stops any back-feed into the ECU's own driver.
#ifndef RELAY_SETTLE_MS
#define RELAY_SETTLE_MS 20
#endif

// ------------------------------------------------------------------ inputs ---
// Momentary button to ground, using the internal pull-up.
#ifndef PIN_BUTTON
#define PIN_BUTTON 27
#endif

// RGB indicator. For a single-colour LED, point all three at the same pin or
// wire only PIN_LED_R and ignore the rest; the blink codes still work.
#ifndef PIN_LED_R
#define PIN_LED_R 32
#endif
#ifndef PIN_LED_G
#define PIN_LED_G 33
#endif
#ifndef PIN_LED_B
#define PIN_LED_B 14
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

// Optional battery sense on an ADC1 input through a divider. Leave
// BATTERY_SENSE_PIN undefined if you did not fit one.
#ifndef PIN_BATTERY_SENSE
#define PIN_BATTERY_SENSE 34
#endif
// Divider ratio: Vbat / Vadc. 100k over 22k gives (100+22)/22 = 5.545.
#ifndef BATTERY_DIVIDER_RATIO
#define BATTERY_DIVIDER_RATIO 5.545f
#endif

// ------------------------------------------------------------------- misc ---
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
// Watchdog timeout. If the main loop stalls the chip resets, outputs go low,
// and the car is back on factory behaviour.
#ifndef WATCHDOG_SECONDS
#define WATCHDOG_SECONDS 4
#endif
#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "s3valve"
#endif
