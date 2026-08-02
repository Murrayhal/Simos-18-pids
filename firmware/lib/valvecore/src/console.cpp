#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace valve {
namespace {

bool parseBool(const char *s, bool &out) {
  if (s == nullptr) return false;
  if (!strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "1") ||
      !strcmp(s, "yes")) {
    out = true;
    return true;
  }
  if (!strcmp(s, "off") || !strcmp(s, "false") || !strcmp(s, "0") ||
      !strcmp(s, "no")) {
    out = false;
    return true;
  }
  return false;
}

bool parseLong(const char *s, long &out) {
  if (s == nullptr || *s == '\0') return false;
  char *end = nullptr;
  const long v = strtol(s, &end, 0);  // 0 => accepts 0x for CAN ids
  if (end == s || (end != nullptr && *end != '\0')) return false;
  out = v;
  return true;
}

bool parseFloat(const char *s, float &out) {
  if (s == nullptr || *s == '\0') return false;
  char *end = nullptr;
  const float v = strtof(s, &end);
  if (end == s || (end != nullptr && *end != '\0')) return false;
  out = v;
  return true;
}

bool parseMode(const char *s, Mode &out) {
  if (s == nullptr) return false;
  if (!strcmp(s, "auto")) { out = Mode::Auto; return true; }
  if (!strcmp(s, "smart")) { out = Mode::Smart; return true; }
  if (!strcmp(s, "open")) { out = Mode::Open; return true; }
  if (!strcmp(s, "quiet")) { out = Mode::Quiet; return true; }
  return false;
}

void lowercase(char *s) {
  for (; *s; ++s) {
    if (*s >= 'A' && *s <= 'Z') *s = static_cast<char>(*s - 'A' + 'a');
  }
}

int tokenize(char *line, char **argv, int maxArgs) {
  int argc = 0;
  char *p = line;
  while (*p && argc < maxArgs) {
    while (*p == ' ' || *p == '\t') *p++ = '\0';
    if (*p == '\0') break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
  }
  return argc;
}

}  // namespace

Console::Console(Settings &settings, ValveController &controller,
                 CanDecoder &decoder, SignalHunter &hunter, PwmMeter &meter,
                 Mode &mode)
    : settings_(settings),
      controller_(controller),
      decoder_(decoder),
      hunter_(hunter),
      meter_(meter),
      mode_(mode),
      write_(nullptr),
      ctx_(nullptr),
      lineLen_(0),
      saveRequested_(false),
      modeChanged_(false),
      huntActive_(false),
      sniffActive_(false),
      sniffFiltered_(false),
      sniffId_(0),
      sniffLastMs_(0) {
  line_[0] = '\0';
}

void Console::setWriter(WriteFn fn, void *ctx) {
  write_ = fn;
  ctx_ = ctx;
}

void Console::print(const char *text) {
  if (write_ != nullptr && text != nullptr) write_(ctx_, text);
}

void Console::printf(const char *fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  print(buf);
}

bool Console::consumeSaveRequest() {
  const bool v = saveRequested_;
  saveRequested_ = false;
  return v;
}

bool Console::consumeModeChanged() {
  const bool v = modeChanged_;
  modeChanged_ = false;
  return v;
}

void Console::greet() {
  print("\r\nS3 8V exhaust valve controller\r\n");
  if (!settings_.actuator.commissioned) {
    print(
        "!! actuator not commissioned - all overrides are disabled\r\n"
        "!! see docs/commissioning.md, then: learn closed / learn open\r\n");
  }
  if (!settings_.canFitted) {
    print("no CAN tap: SMART unavailable, bus interlocks skipped\r\n");
  } else if (!signalsCommissioned(settings_)) {
    print(
        "!! CAN signals not fully configured - run `hunt` on the car\r\n"
        "!! see docs/can-signals.md\r\n");
  }
  print("type `help`\r\n> ");
}

void Console::feed(char c, uint32_t nowMs) {
  if (c == '\r' || c == '\n') {
    if (lineLen_ == 0) {
      print("> ");
      return;
    }
    line_[lineLen_] = '\0';
    print("\r\n");
    handleLine(line_, nowMs);
    lineLen_ = 0;
    line_[0] = '\0';
    print("> ");
    return;
  }
  if (c == 8 || c == 127) {  // backspace
    if (lineLen_ > 0) {
      lineLen_--;
      print("\b \b");
    }
    return;
  }
  if (c < 32) return;
  if (static_cast<size_t>(lineLen_) + 1 >= sizeof(line_)) return;
  line_[lineLen_++] = c;
}

void Console::handleLine(const char *line, uint32_t nowMs) {
  char buf[128];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *argv[10];
  const int argc = tokenize(buf, argv, 10);
  if (argc == 0) return;
  lowercase(argv[0]);

  if (!strcmp(argv[0], "help") || !strcmp(argv[0], "?")) return cmdHelp();
  if (!strcmp(argv[0], "status")) return cmdStatus();
  if (!strcmp(argv[0], "mode")) return cmdMode(argc, argv);
  if (!strcmp(argv[0], "set")) return cmdSet(argc, argv);
  if (!strcmp(argv[0], "show")) return cmdShow();
  if (!strcmp(argv[0], "sig")) return cmdSig(argc, argv);
  if (!strcmp(argv[0], "test")) return cmdTest(argc, argv, nowMs);
  if (!strcmp(argv[0], "probe")) return cmdProbe();
  if (!strcmp(argv[0], "learn")) return cmdLearn(argc, argv);
  if (!strcmp(argv[0], "hunt")) return cmdHunt(argc, argv);
  if (!strcmp(argv[0], "sniff")) return cmdSniff(argc, argv);
  if (!strcmp(argv[0], "can")) return cmdCan();
  if (!strcmp(argv[0], "save")) {
    saveRequested_ = true;
    print("saving\r\n");
    return;
  }
  if (!strcmp(argv[0], "defaults")) {
    loadDefaults(settings_);
    mode_ = settings_.defaultMode;
    modeChanged_ = true;
    print("settings reset to defaults (not yet saved)\r\n");
    return;
  }
  printf("unknown command: %s\r\n", argv[0]);
}

void Console::cmdHelp() {
  print(
      "status                      current mode, outputs, decoded signals\r\n"
      "mode [auto|smart|open|quiet]\r\n"
      "show                        dump settings as pasteable commands\r\n"
      "set <key> <value>           see docs/operation.md for keys\r\n"
      "sig <name> <id> <startbit> <len> <le|be> <scale> <offset>\r\n"
      "sig <name> [off]            show or disable one signal\r\n"
      "probe                       measure the ECU's PWM on the signal line\r\n"
      "learn open|closed           store the ECU's current command\r\n"
      "test duty <percent> [seconds] | test stock   engine off only\r\n"
      "hunt start|mark <value>|top [n]|stop\r\n"
      "sniff on [id]|off           dump raw frames\r\n"
      "can                         bus statistics\r\n"
      "save                        persist settings\r\n"
      "defaults                    restore factory settings\r\n");
}

bool Console::engineLikelyRunning() const {
  const VehicleState &vs = decoder_.state();
  return vs.rpmValid && vs.rpm >= settings_.safety.engineRunningRpm;
}

void Console::cmdStatus() {
  const ControllerOutput &o = controller_.last();
  const VehicleState &vs = decoder_.state();

  printf("mode      %s%s\r\n", modeName(mode_),
         o.testActive ? "  (BENCH TEST ACTIVE)" : "");
  printf("target    %s   relay %s   command %.1f %%\r\n", targetName(o.target),
         o.interceptRelay ? "INTERCEPT" : "stock",
         o.interceptRelay ? o.commandDuty / 10.0 : 0.0);
  printf("lockout   %s\r\n", lockoutName(o.lockout));
  printf("actuator  %u Hz, open %.1f %%, closed %.1f %%%s\r\n",
         static_cast<unsigned>(settings_.actuator.pwmHz),
         settings_.actuator.openDutyTenths / 10.0,
         settings_.actuator.closedDutyTenths / 10.0,
         settings_.actuator.commissioned ? "" : "  (NOT COMMISSIONED)");
  if (meter_.valid()) {
    printf("ecu line  %u Hz, %.1f %%%s\r\n",
           static_cast<unsigned>(meter_.frequencyHz()), meter_.duty() / 10.0,
           meter_.stable() ? "" : "  (changing)");
  } else {
    printf("ecu line  no PWM (%lu edges seen)\r\n",
           static_cast<unsigned long>(meter_.edgeCount()));
  }

  printf("rpm       %s", vs.rpmValid ? "" : "(stale) ");
  printf("%u\r\n", static_cast<unsigned>(vs.rpm));
  printf("pedal     %s%u %%\r\n", vs.pedalValid ? "" : "(stale) ",
         static_cast<unsigned>(vs.pedalPct));
  printf("speed     %s%u km/h\r\n", vs.speedValid ? "" : "(stale) ",
         static_cast<unsigned>(vs.speedKph));
  printf("coolant   %s%d C\r\n", vs.coolantValid ? "" : "(stale) ",
         static_cast<int>(vs.coolantC));
  printf("drive     %s%u\r\n", vs.driveSelectValid ? "" : "(stale) ",
         static_cast<unsigned>(vs.driveSelectRaw));
  printf("frames    %lu\r\n",
         static_cast<unsigned long>(decoder_.frameCount()));
}

void Console::cmdMode(int argc, char **argv) {
  if (argc < 2) {
    printf("mode %s\r\n", modeName(mode_));
    return;
  }
  lowercase(argv[1]);
  Mode m;
  if (!parseMode(argv[1], m)) {
    print("usage: mode auto|smart|open|quiet\r\n");
    return;
  }
  if (!modeAvailable(settings_, m)) {
    print("SMART needs bus data. `set can on` and configure signals first.\r\n");
    return;
  }
  mode_ = m;
  modeChanged_ = true;
  printf("mode %s\r\n", modeName(mode_));
}

void Console::cmdShow() {
  const Settings &s = settings_;
  printf("set actuator.pwmhz %u\r\n", static_cast<unsigned>(s.actuator.pwmHz));
  printf("set actuator.openduty %.1f\r\n", s.actuator.openDutyTenths / 10.0);
  printf("set actuator.closedduty %.1f\r\n",
         s.actuator.closedDutyTenths / 10.0);
  printf("set confirm %s\r\n", s.actuator.commissioned ? "on" : "off");
  printf("set can %s\r\n", s.canFitted ? "on" : "off");
  printf("set default %s\r\n", modeName(s.defaultMode));
  printf("set smart.openrpm %u\r\n", static_cast<unsigned>(s.smart.openRpm));
  printf("set smart.closerpm %u\r\n", static_cast<unsigned>(s.smart.closeRpm));
  printf("set smart.openpedal %u\r\n",
         static_cast<unsigned>(s.smart.openPedalPct));
  printf("set smart.closepedal %u\r\n",
         static_cast<unsigned>(s.smart.closePedalPct));
  printf("set smart.quietkph %u\r\n",
         static_cast<unsigned>(s.smart.quietBelowKph));
  printf("set smart.drive %s\r\n", s.smart.followDriveSelect ? "on" : "off");
  printf("set smart.dynraw %u\r\n",
         static_cast<unsigned>(s.smart.dynamicRawValue));
  printf("set safety.coolant %d\r\n", static_cast<int>(s.safety.minCoolantC));
  printf("set safety.timeout %u\r\n",
         static_cast<unsigned>(s.safety.signalTimeoutMs));
  printf("set safety.requirecan %s\r\n",
         s.safety.requireCanForOverride ? "on" : "off");
  printf("set safety.requireengine %s\r\n",
         s.safety.requireEngineRunning ? "on" : "off");
  printf("set safety.runrpm %u\r\n",
         static_cast<unsigned>(s.safety.engineRunningRpm));
  printf("set safety.startupquiet %u\r\n",
         static_cast<unsigned>(s.safety.startupQuietMs));
  printf("set safety.dwell %u\r\n", static_cast<unsigned>(s.safety.minDwellMs));
  printf("set safety.batsense %s\r\n",
         s.safety.batterySenseFitted ? "on" : "off");
  printf("set safety.minmv %u\r\n", static_cast<unsigned>(s.safety.minBatteryMv));
  printf("set safety.maxmv %u\r\n", static_cast<unsigned>(s.safety.maxBatteryMv));
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    const SignalDef &d = s.signals[i];
    const char *name = signalName(static_cast<SignalId>(i));
    if (!d.enabled) {
      printf("sig %s off\r\n", name);
      continue;
    }
    printf("sig %s 0x%03lX %u %u %s %g %g\r\n", name,
           static_cast<unsigned long>(d.canId),
           static_cast<unsigned>(d.startBit),
           static_cast<unsigned>(d.bitLength),
           d.endian == Endian::Big ? "be" : "le",
           static_cast<double>(d.scale), static_cast<double>(d.offset));
  }
}

void Console::cmdSet(int argc, char **argv) {
  if (argc < 3) {
    print("usage: set <key> <value>   (`show` lists every key)\r\n");
    return;
  }
  lowercase(argv[1]);
  lowercase(argv[2]);
  const char *k = argv[1];
  const char *v = argv[2];
  Settings &s = settings_;

  long n = 0;
  bool b = false;

  // The duty keys exist so a known-good configuration can be pasted back in
  // from `show`, or entered from a scope reading. `learn` is the normal route.
  if (!strcmp(k, "actuator.openduty") || !strcmp(k, "actuator.closedduty")) {
    float pct = 0.0f;
    if (!parseFloat(v, pct) || pct < 0.0f || pct > 100.0f) {
      print("expected 0..100\r\n");
      return;
    }
    const DutyTenths duty = static_cast<DutyTenths>(pct * 10.0f + 0.5f);
    if (!strcmp(k, "actuator.openduty")) {
      s.actuator.openDutyTenths = duty;
      s.actuator.openLearned = true;
    } else {
      s.actuator.closedDutyTenths = duty;
      s.actuator.closedLearned = true;
    }
    s.actuator.commissioned = s.actuator.openLearned &&
                              s.actuator.closedLearned &&
                              s.actuator.pwmHz > 0 &&
                              s.actuator.openDutyTenths !=
                                  s.actuator.closedDutyTenths;
    printf("%s = %.1f %%%s\r\n", k, duty / 10.0,
           s.actuator.commissioned ? ", overrides enabled" : "");
    return;
  }
  if (!strcmp(k, "actuator.pwmhz")) {
    if (!parseLong(v, n) || n < 1 || n > 20000) {
      print("expected 1..20000 Hz\r\n");
      return;
    }
    s.actuator.pwmHz = static_cast<uint16_t>(n);
    printf("actuator.pwmhz = %ld\r\n", n);
    return;
  }
  if (!strcmp(k, "confirm")) {
    if (!parseBool(v, b)) { print("expected on|off\r\n"); return; }
    if (b && !(s.actuator.openLearned && s.actuator.closedLearned &&
               s.actuator.pwmHz > 0)) {
      print("refused: both end positions and a carrier frequency are needed "
            "first. Run `learn open` and `learn closed`.\r\n");
      return;
    }
    s.actuator.commissioned = b;
    printf("actuator %s\r\n", b ? "commissioned" : "not commissioned");
    return;
  }
  if (!strcmp(k, "can")) {
    if (!parseBool(v, b)) { print("expected on|off\r\n"); return; }
    s.canFitted = b;
    if (!b && !modeAvailable(s, mode_)) mode_ = Mode::Auto;
    if (!b && s.defaultMode == Mode::Smart) s.defaultMode = Mode::Auto;
    printf("CAN tap %s\r\n", b ? "fitted" : "not fitted");
    if (!b) {
      print("SMART disabled; bus interlocks skipped. Use a switched 12V feed."
            "\r\n");
    }
    return;
  }
  if (!strcmp(k, "default")) {
    Mode m;
    if (!parseMode(v, m)) { print("expected auto|smart|open|quiet\r\n"); return; }
    if (!modeAvailable(s, m)) {
      print("SMART needs bus data. `set can on` first.\r\n");
      return;
    }
    s.defaultMode = m;
    printf("default mode %s\r\n", modeName(m));
    return;
  }

#define SET_U(key, field, lo, hi)                                    \
  if (!strcmp(k, key)) {                                             \
    if (!parseLong(v, n) || n < (lo) || n > (hi)) {                  \
      printf("expected %ld..%ld\r\n", (long)(lo), (long)(hi));       \
      return;                                                        \
    }                                                                \
    field = static_cast<decltype(field)>(n);                         \
    printf("%s = %ld\r\n", key, n);                                  \
    return;                                                          \
  }
#define SET_B(key, field)                                            \
  if (!strcmp(k, key)) {                                             \
    if (!parseBool(v, b)) { print("expected on|off\r\n"); return; }  \
    field = b;                                                       \
    printf("%s = %s\r\n", key, b ? "on" : "off");                    \
    return;                                                          \
  }

  SET_U("smart.openrpm", s.smart.openRpm, 0, 9000)
  SET_U("smart.closerpm", s.smart.closeRpm, 0, 9000)
  SET_U("smart.openpedal", s.smart.openPedalPct, 0, 100)
  SET_U("smart.closepedal", s.smart.closePedalPct, 0, 100)
  SET_U("smart.quietkph", s.smart.quietBelowKph, 0, 300)
  SET_B("smart.drive", s.smart.followDriveSelect)
  SET_U("smart.dynraw", s.smart.dynamicRawValue, 0, 255)
  SET_U("safety.coolant", s.safety.minCoolantC, -273, 200)
  SET_U("safety.timeout", s.safety.signalTimeoutMs, 50, 60000)
  SET_B("safety.requirecan", s.safety.requireCanForOverride)
  SET_B("safety.requireengine", s.safety.requireEngineRunning)
  SET_U("safety.runrpm", s.safety.engineRunningRpm, 0, 3000)
  SET_U("safety.startupquiet", s.safety.startupQuietMs, 0, 60000)
  SET_U("safety.dwell", s.safety.minDwellMs, 0, 10000)
  SET_B("safety.batsense", s.safety.batterySenseFitted)
  SET_U("safety.minmv", s.safety.minBatteryMv, 6000, 16000)
  SET_U("safety.maxmv", s.safety.maxBatteryMv, 6000, 20000)

#undef SET_U
#undef SET_B

  printf("unknown key: %s\r\n", k);
}

void Console::cmdSig(int argc, char **argv) {
  if (argc < 2) {
    print("usage: sig <rpm|pedal|speed|coolant|drive> ...\r\n");
    return;
  }
  lowercase(argv[1]);
  const SignalId id = signalFromName(argv[1]);
  if (id == SignalId::Count) {
    printf("unknown signal: %s\r\n", argv[1]);
    return;
  }
  SignalDef &d = settings_.signals[static_cast<uint8_t>(id)];

  if (argc == 2) {
    if (!d.enabled) {
      printf("%s disabled\r\n", signalName(id));
      return;
    }
    printf("%s id 0x%03lX startbit %u len %u %s scale %g offset %g\r\n",
           signalName(id), static_cast<unsigned long>(d.canId),
           static_cast<unsigned>(d.startBit), static_cast<unsigned>(d.bitLength),
           d.endian == Endian::Big ? "be" : "le", static_cast<double>(d.scale),
           static_cast<double>(d.offset));
    return;
  }

  lowercase(argv[2]);
  if (!strcmp(argv[2], "off")) {
    d.enabled = false;
    printf("%s disabled\r\n", signalName(id));
    return;
  }

  if (argc < 8) {
    print("usage: sig <name> <id> <startbit> <len> <le|be> <scale> <offset>\r\n");
    return;
  }

  long canId = 0, startBit = 0, bitLength = 0;
  float scale = 0.0f, offset = 0.0f;
  lowercase(argv[5]);
  const bool big = !strcmp(argv[5], "be");
  if (!big && strcmp(argv[5], "le") != 0) {
    print("byte order must be le or be\r\n");
    return;
  }
  if (!parseLong(argv[2], canId) || canId < 0 || canId > 0x1FFFFFFF) {
    print("bad can id\r\n");
    return;
  }
  if (!parseLong(argv[3], startBit) || startBit < 0 || startBit > 63) {
    print("start bit must be 0..63\r\n");
    return;
  }
  if (!parseLong(argv[4], bitLength) || bitLength < 1 || bitLength > 32) {
    print("length must be 1..32\r\n");
    return;
  }
  if (!parseFloat(argv[6], scale) || !parseFloat(argv[7], offset)) {
    print("bad scale or offset\r\n");
    return;
  }

  d.canId = static_cast<uint32_t>(canId);
  d.startBit = static_cast<uint8_t>(startBit);
  d.bitLength = static_cast<uint8_t>(bitLength);
  d.endian = big ? Endian::Big : Endian::Little;
  d.scale = scale;
  d.offset = offset;
  d.enabled = true;
  printf("%s configured (remember to `save`)\r\n", signalName(id));
}

void Console::cmdTest(int argc, char **argv, uint32_t nowMs) {
  if (argc < 2) {
    print("usage: test duty <percent> [seconds] | test stock\r\n");
    return;
  }
  lowercase(argv[1]);

  if (!strcmp(argv[1], "stock")) {
    controller_.stopTest();
    print("test cleared, outputs back under normal control\r\n");
    return;
  }

  // Driving the actuator by hand while the engine is running fights the ECU
  // and tells you nothing useful, so refuse it.
  if (engineLikelyRunning()) {
    print("refused: engine is running. Test with the engine off.\r\n");
    return;
  }

  if (strcmp(argv[1], "duty") != 0) {
    print("usage: test duty <percent> [seconds] | test stock\r\n");
    return;
  }

  float pct = 0.0f;
  if (argc < 3 || !parseFloat(argv[2], pct) || pct < 0.0f || pct > 100.0f) {
    print("percent must be 0..100\r\n");
    return;
  }
  if (settings_.actuator.pwmHz == 0) {
    print("no carrier frequency yet. Run `probe` with the ignition on first, "
          "or `set actuator.pwmhz <hz>`.\r\n");
    return;
  }

  long seconds = 10;
  if (argc >= 4 && (!parseLong(argv[3], seconds) || seconds < 1 || seconds > 60)) {
    print("seconds must be 1..60\r\n");
    return;
  }

  const DutyTenths duty = static_cast<DutyTenths>(pct * 10.0f + 0.5f);
  controller_.startTest(true, duty, static_cast<uint32_t>(seconds) * 1000u,
                        nowMs);
  printf("intercept relay on, driving %.1f %% at %u Hz for %ld s\r\n",
         duty / 10.0, static_cast<unsigned>(settings_.actuator.pwmHz), seconds);
}

void Console::cmdProbe() {
  if (meter_.edgeCount() == 0) {
    print("no edges on the signal line at all.\r\n"
          "  - ignition on? the ECU only drives the actuator when awake\r\n"
          "  - is the sense input on the right wire? it is the one that is\r\n"
          "    neither 12 V nor ground with the connector back-probed\r\n");
    return;
  }
  if (!meter_.valid()) {
    printf("line has switched %lu times but is idle now (parked high or low)\r\n",
           static_cast<unsigned long>(meter_.edgeCount()));
    return;
  }
  printf("%u Hz, duty %.1f %%, %lu edges%s\r\n",
         static_cast<unsigned>(meter_.frequencyHz()), meter_.duty() / 10.0,
         static_cast<unsigned long>(meter_.edgeCount()),
         meter_.stable() ? ", steady" : ", CHANGING - hold still and re-read");
}

void Console::cmdLearn(int argc, char **argv) {
  if (argc < 2) {
    print("usage: learn open|closed\r\n"
          "Put the car in the drive mode that gives you that flap position,\r\n"
          "let it settle, then run the command.\r\n");
    return;
  }
  lowercase(argv[1]);
  const bool wantOpen = !strcmp(argv[1], "open");
  if (!wantOpen && strcmp(argv[1], "closed") != 0) {
    print("usage: learn open|closed\r\n");
    return;
  }

  if (!meter_.valid()) {
    print("nothing to learn: no PWM on the signal line. Run `probe`.\r\n");
    return;
  }
  if (!meter_.stable()) {
    print("refused: the command is still changing. Let the ECU settle on one "
          "position, then try again.\r\n");
    return;
  }

  ActuatorSettings &a = settings_.actuator;
  a.pwmHz = meter_.frequencyHz();
  if (wantOpen) {
    a.openDutyTenths = meter_.duty();
    a.openLearned = true;
  } else {
    a.closedDutyTenths = meter_.duty();
    a.closedLearned = true;
  }

  printf("learned %s = %.1f %% at %u Hz\r\n", wantOpen ? "open" : "closed",
         meter_.duty() / 10.0, static_cast<unsigned>(a.pwmHz));

  if (!a.openLearned || !a.closedLearned) {
    printf("still need the %s position.\r\n", a.openLearned ? "closed" : "open");
    return;
  }
  if (a.openDutyTenths == a.closedDutyTenths) {
    a.commissioned = false;
    print("open and closed came out identical, so one was captured in the "
          "wrong drive mode. Not commissioned.\r\n");
    return;
  }
  a.commissioned = true;
  print("both positions known, overrides enabled. `save` to keep them.\r\n");
}

void Console::cmdHunt(int argc, char **argv) {
  if (argc < 2) {
    print("usage: hunt start|mark <value>|top [n]|stop\r\n");
    return;
  }
  lowercase(argv[1]);

  if (!strcmp(argv[1], "start")) {
    hunter_.reset();
    huntActive_ = true;
    printf("hunting. Hold a steady operating point, then `hunt mark <value>`."
           "\r\nTake at least %u marks at different values.\r\n",
           static_cast<unsigned>(kHunterMinMarks));
    return;
  }
  if (!strcmp(argv[1], "stop")) {
    huntActive_ = false;
    print("hunt stopped\r\n");
    return;
  }
  if (!strcmp(argv[1], "mark")) {
    if (!huntActive_) {
      print("run `hunt start` first\r\n");
      return;
    }
    float value = 0.0f;
    if (argc < 3 || !parseFloat(argv[2], value)) {
      print("usage: hunt mark <the value you are holding, e.g. 2000>\r\n");
      return;
    }
    if (!hunter_.mark(value)) {
      printf("mark table full (%u marks)\r\n",
             static_cast<unsigned>(kHunterMaxMarks));
      return;
    }
    printf("mark %u recorded at %g over %lu ids / %lu frames\r\n",
           static_cast<unsigned>(hunter_.markCount()),
           static_cast<double>(value),
           static_cast<unsigned long>(hunter_.idCount()),
           static_cast<unsigned long>(hunter_.frameCount()));
    if (hunter_.overflowed()) {
      printf("warning: more than %u ids on the bus, some were ignored\r\n",
             static_cast<unsigned>(kHunterMaxIds));
    }
    return;
  }
  if (!strcmp(argv[1], "top")) {
    long n = 5;
    if (argc >= 3 && (!parseLong(argv[2], n) || n < 1 || n > 20)) {
      print("n must be 1..20\r\n");
      return;
    }
    HuntResult results[20];
    const uint8_t count = hunter_.rank(results, static_cast<uint8_t>(n));
    if (count == 0) {
      printf("no candidates. Need at least %u marks at different values.\r\n",
             static_cast<unsigned>(kHunterMinMarks));
      return;
    }
    print("best fits (paste the sig line for the one that makes sense):\r\n");
    for (uint8_t i = 0; i < count; ++i) {
      const HuntResult &r = results[i];
      printf("  err %8.2f   sig <name> 0x%03lX %u %u %s %g %g\r\n",
             static_cast<double>(r.error),
             static_cast<unsigned long>(r.canId),
             static_cast<unsigned>(r.startBit),
             static_cast<unsigned>(r.bitLength),
             r.endian == Endian::Big ? "be" : "le",
             static_cast<double>(r.scale), static_cast<double>(r.offset));
    }
    return;
  }
  print("usage: hunt start|mark <value>|top [n]|stop\r\n");
}

void Console::cmdSniff(int argc, char **argv) {
  if (argc < 2) {
    print("usage: sniff on [id]|off\r\n");
    return;
  }
  lowercase(argv[1]);
  if (!strcmp(argv[1], "off")) {
    sniffActive_ = false;
    print("sniff off\r\n");
    return;
  }
  if (strcmp(argv[1], "on") != 0) {
    print("usage: sniff on [id]|off\r\n");
    return;
  }
  sniffFiltered_ = false;
  sniffId_ = 0;
  if (argc >= 3) {
    long id = 0;
    if (!parseLong(argv[2], id) || id < 0) {
      print("bad can id\r\n");
      return;
    }
    sniffId_ = static_cast<uint32_t>(id);
    sniffFiltered_ = true;
  }
  sniffActive_ = true;
  print("sniff on (rate limited). `sniff off` to stop.\r\n");
}

void Console::cmdCan() {
  printf("frames decoded into signals: %lu\r\n",
         static_cast<unsigned long>(decoder_.frameCount()));
  printf("hunter: %s, %lu ids, %lu frames, %u marks\r\n",
         huntActive_ ? "running" : "idle",
         static_cast<unsigned long>(hunter_.idCount()),
         static_cast<unsigned long>(hunter_.frameCount()),
         static_cast<unsigned>(hunter_.markCount()));
}

void Console::onFrame(const CanFrame &frame, uint32_t nowMs) {
  if (huntActive_) hunter_.onFrame(frame);
  if (!sniffActive_) return;
  if (sniffFiltered_ && frame.id != sniffId_) return;
  // Without a limit a 500 kbit/s powertrain bus buries the console instantly.
  if ((nowMs - sniffLastMs_) < 50) return;
  sniffLastMs_ = nowMs;

  char hex[3 * 8 + 1];
  int pos = 0;
  const uint8_t dlc = frame.dlc > 8 ? 8 : frame.dlc;
  for (uint8_t i = 0; i < dlc; ++i) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", frame.data[i]);
  }
  hex[pos > 0 ? pos - 1 : 0] = '\0';
  printf("0x%03lX [%u] %s\r\n", static_cast<unsigned long>(frame.id),
         static_cast<unsigned>(dlc), hex);
}

}  // namespace valve
