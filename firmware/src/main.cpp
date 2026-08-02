// S3 8V exhaust valve controller - ESP32 platform layer.
//
// All of the decision making lives in lib/valvecore, which is plain C++ and
// unit tested on the host. This file is only wiring: CAN in, buttons in,
// two output pins out, settings in NVS.

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <string.h>
#include <mcp_can.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

#include "board_config.h"
#include "button.h"
#include "can_decode.h"
#include "console.h"
#include "led_indicator.h"
#include "pwm_meter.h"
#include "settings.h"
#include "signal_hunter.h"
#include "valve_controller.h"

using namespace valve;

namespace {

Settings g_settings;
Mode g_mode = Mode::Auto;

CanDecoder g_decoder(g_settings);
ValveController g_controller(g_settings);
SignalHunter g_hunter;
PwmMeter g_meter;
Console g_console(g_settings, g_controller, g_decoder, g_hunter, g_meter,
                  g_mode);
Button g_button;
LedIndicator g_led;

MCP_CAN g_can(PIN_CAN_CS);
Preferences g_prefs;

bool g_canUp = false;
uint32_t g_lastCanRetryMs = 0;

// Output sequencing state. The relay and the PWM driver are never allowed to
// move in the same instant; see RELAY_SETTLE_MS.
bool g_relayOn = false;
bool g_pwmOn = false;
DutyTenths g_pwmDuty = 0;
uint16_t g_pwmHz = 0;
uint32_t g_relayChangedMs = 0;

// ---------------------------------------------------------------- storage ---

void settingsSave() {
  g_settings.version = kSettingsVersion;
  if (!g_prefs.begin(NVS_NAMESPACE, false)) {
    g_console.print("NVS open failed, settings not saved\r\n");
    return;
  }
  const size_t written =
      g_prefs.putBytes("cfg", &g_settings, sizeof(g_settings));
  g_prefs.end();
  if (written == sizeof(g_settings)) {
    g_console.print("saved\r\n");
  } else {
    g_console.print("NVS write short, settings not saved\r\n");
  }
}

void settingsLoad() {
  loadDefaults(g_settings);
  if (!g_prefs.begin(NVS_NAMESPACE, true)) return;
  Settings stored;
  const size_t read = g_prefs.getBytes("cfg", &stored, sizeof(stored));
  g_prefs.end();
  if (read == sizeof(stored) && stored.version == kSettingsVersion) {
    g_settings = stored;
  }
  // Anything else (no blob, wrong size, older version) leaves the safe
  // defaults in place, which means an unconfigured controller.
}

// ----------------------------------------------------------------- outputs ---

void writeRelay(bool on) {
#if RELAY_ACTIVE_LOW
  digitalWrite(PIN_RELAY, on ? LOW : HIGH);
#else
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
#endif
}

// Edge capture on the ECU's command line.
//
// The ISR does not touch PwmMeter: that code lives in flash, and calling into
// flash from an interrupt deadlocks the chip whenever a flash operation (an NVS
// write, say) happens to be in progress. So the handler only stamps a
// timestamp and level into an IRAM ring buffer, and the main loop drains it.
struct Edge {
  uint32_t us;
  bool level;
};
const uint16_t kEdgeQueueLen = 128;
volatile Edge g_edges[kEdgeQueueLen];
volatile uint16_t g_edgeHead = 0;  // written by the ISR only
volatile uint16_t g_edgeTail = 0;  // written by the loop only
volatile uint32_t g_edgesDropped = 0;

void IRAM_ATTR onSenseEdge() {
  const uint16_t head = g_edgeHead;
  const uint16_t next = static_cast<uint16_t>((head + 1) % kEdgeQueueLen);
  if (next == g_edgeTail) {
    g_edgesDropped++;  // loop is behind; better to lose edges than to block
    return;
  }
  g_edges[head].us = micros();
  g_edges[head].level = digitalRead(PIN_ECU_SENSE) == HIGH;
  g_edgeHead = next;
}

void drainEdges() {
  while (g_edgeTail != g_edgeHead) {
    const Edge e = {g_edges[g_edgeTail].us, g_edges[g_edgeTail].level};
    g_edgeTail = static_cast<uint16_t>((g_edgeTail + 1) % kEdgeQueueLen);
    g_meter.onEdge(e.us, e.level);
  }
}

// Stops driving the actuator line entirely. Used whenever we are not
// intercepting, so we are never fighting the ECU on a shared wire.
void pwmIdle() {
  if (!g_pwmOn) return;
  ledcWrite(ACTUATOR_LEDC_CHANNEL, ACTUATOR_PWM_INVERTED ? (1u << ACTUATOR_LEDC_BITS) - 1u : 0);
  ledcDetachPin(PIN_ACTUATOR_PWM);
  pinMode(PIN_ACTUATOR_PWM, INPUT);
  g_pwmOn = false;
  g_pwmDuty = 0;
}

void pwmDrive(uint16_t hz, DutyTenths duty) {
  if (hz == 0) {
    pwmIdle();
    return;
  }
  if (!g_pwmOn || hz != g_pwmHz) {
    ledcSetup(ACTUATOR_LEDC_CHANNEL, hz, ACTUATOR_LEDC_BITS);
    ledcAttachPin(PIN_ACTUATOR_PWM, ACTUATOR_LEDC_CHANNEL);
    g_pwmHz = hz;
    g_pwmOn = true;
    g_pwmDuty = static_cast<DutyTenths>(~duty);  // force the write below
  }
  if (duty == g_pwmDuty) return;
  const uint32_t full = (1u << ACTUATOR_LEDC_BITS) - 1u;
  uint32_t counts = (static_cast<uint32_t>(duty) * full) / kDutyMax;
#if ACTUATOR_PWM_INVERTED
  counts = full - counts;
#endif
  ledcWrite(ACTUATOR_LEDC_CHANNEL, counts);
  g_pwmDuty = duty;
}

void writeLed(const LedOut &c) {
#if LED_ACTIVE_LOW
  digitalWrite(PIN_LED_R, c.r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, c.g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, c.b ? LOW : HIGH);
#else
  digitalWrite(PIN_LED_R, c.r ? HIGH : LOW);
  digitalWrite(PIN_LED_G, c.g ? HIGH : LOW);
  digitalWrite(PIN_LED_B, c.b ? HIGH : LOW);
#endif
}

// Applies the controller's wishes with make-before-break sequencing:
//   taking control   relay on, wait for the contacts, then start the PWM
//   handing back     PWM off first, then relay off
void applyOutputs(const ControllerOutput &out, uint32_t nowMs) {
  if (!out.interceptRelay) {
    pwmIdle();
    if (g_relayOn) {
      g_relayOn = false;
      g_relayChangedMs = nowMs;
      writeRelay(false);
    }
    return;
  }

  if (!g_relayOn) {
    g_relayOn = true;
    g_relayChangedMs = nowMs;
    writeRelay(true);
    // The command waits for the contacts to finish transferring.
    pwmIdle();
    return;
  }

  if ((nowMs - g_relayChangedMs) < RELAY_SETTLE_MS) return;

  pwmDrive(g_settings.actuator.pwmHz, out.commandDuty);
}

// --------------------------------------------------------------------- CAN ---

bool canBegin() {
#if CAN_CRYSTAL_MHZ == 16
  const uint8_t clock = MCP_16MHZ;
#elif CAN_CRYSTAL_MHZ == 20
  const uint8_t clock = MCP_20MHZ;
#else
  const uint8_t clock = MCP_8MHZ;
#endif
#if CAN_BITRATE_KBPS == 250
  const uint8_t speed = CAN_250KBPS;
#elif CAN_BITRATE_KBPS == 125
  const uint8_t speed = CAN_125KBPS;
#else
  const uint8_t speed = CAN_500KBPS;
#endif
  if (g_can.begin(MCP_ANY, speed, clock) != CAN_OK) return false;
  // Listen only: this controller has no business transmitting onto a
  // powertrain bus that is also carrying braking and steering traffic.
  g_can.setMode(MCP_LISTENONLY);
  return true;
}

void serviceCanRx(uint32_t nowMs) {
  // Drain a bounded number of frames per loop so a busy bus cannot starve the
  // button, the console or the watchdog.
  for (uint8_t i = 0; i < 16; ++i) {
    if (g_can.checkReceive() != CAN_MSGAVAIL) return;
    unsigned long id = 0;
    uint8_t ext = 0;
    uint8_t len = 0;
    uint8_t buf[8];
    if (g_can.readMsgBufID(&id, &len, buf) != CAN_OK) return;
    (void)ext;

    CanFrame frame;
    frame.id = static_cast<uint32_t>(id) & 0x1FFFFFFFu;
    frame.dlc = len > 8 ? 8 : len;
    memset(frame.data, 0, sizeof(frame.data));
    memcpy(frame.data, buf, frame.dlc);

    g_decoder.onFrame(frame, nowMs);
    g_console.onFrame(frame, nowMs);
  }
}

// ------------------------------------------------------------------- misc ---

uint16_t readBatteryMv() {
#ifdef PIN_BATTERY_SENSE
  if (!g_settings.safety.batterySenseFitted) return 0;
  const uint32_t mv = analogReadMilliVolts(PIN_BATTERY_SENSE);
  const float v = static_cast<float>(mv) * BATTERY_DIVIDER_RATIO;
  return static_cast<uint16_t>(v > 65535.0f ? 65535.0f : v);
#else
  return 0;
#endif
}

void consoleWrite(void *ctx, const char *text) {
  (void)ctx;
  Serial.print(text);
}

void feedWatchdog() {
#if defined(ESP32) && defined(CONFIG_ESP_TASK_WDT)
  esp_task_wdt_reset();
#elif defined(ESP32)
  esp_task_wdt_reset();
#endif
}

void startWatchdog() {
#if defined(ESP32)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WATCHDOG_SECONDS * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&cfg);
  esp_task_wdt_add(NULL);
#else
  esp_task_wdt_init(WATCHDOG_SECONDS, true);
  esp_task_wdt_add(NULL);
#endif
#endif
}

}  // namespace

void setup() {
  // Outputs first and low, before anything that could take time or fail. A
  // half-initialised controller must not be holding the flap anywhere.
  pinMode(PIN_RELAY, OUTPUT);
  writeRelay(false);
  // High impedance until we have a reason to drive it, so a boot with the
  // relay stuck closed still cannot inject a command.
  pinMode(PIN_ACTUATOR_PWM, INPUT);
  pinMode(PIN_ECU_SENSE, INPUT);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  writeLed(LedOut());
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(SERIAL_BAUD);

  settingsLoad();
  g_mode = g_settings.defaultMode;

  g_console.setWriter(consoleWrite, nullptr);

  if (g_settings.canFitted) {
    SPI.begin();
    g_canUp = canBegin();
  }

  attachInterrupt(digitalPinToInterrupt(PIN_ECU_SENSE), onSenseEdge, CHANGE);

  g_controller.begin(millis());
  g_console.greet();
  if (g_settings.canFitted && !g_canUp) {
    g_console.print("!! MCP2515 did not initialise, check wiring/crystal\r\n");
  }

  startWatchdog();
}

void loop() {
  const uint32_t now = millis();

  if (g_settings.canFitted && !g_canUp && (now - g_lastCanRetryMs) > 2000) {
    g_lastCanRetryMs = now;
    g_canUp = canBegin();
    if (g_canUp) g_console.print("CAN up\r\n");
  }
  if (g_canUp) serviceCanRx(now);
  g_decoder.tick(now);
  drainEdges();
  g_meter.tick(micros());

  while (Serial.available() > 0) {
    g_console.feed(static_cast<char>(Serial.read()), now);
  }
  if (g_console.consumeSaveRequest()) settingsSave();

  const bool pressed = digitalRead(PIN_BUTTON) == LOW;
  switch (g_button.update(now, pressed)) {
    case ButtonEvent::Short:
      g_mode = nextMode(g_settings, g_mode);
      g_console.printf("mode %s\r\n", modeName(g_mode));
      break;
    case ButtonEvent::Long:
      g_settings.defaultMode = g_mode;
      settingsSave();
      g_led.requestFlash(3);
      g_console.printf("default mode saved as %s\r\n", modeName(g_mode));
      break;
    default:
      break;
  }
  (void)g_console.consumeModeChanged();

  const ControllerOutput out =
      g_controller.update(now, g_mode, g_decoder.state(), readBatteryMv());
  applyOutputs(out, now);
  writeLed(g_led.update(now, g_mode, out));

  feedWatchdog();
}
