// Line-oriented serial console for configuration and commissioning.
//
// Kept free of Arduino types so the command parser can be exercised on a host
// with a string-capturing writer.
#pragma once

#include "can_decode.h"
#include "settings.h"
#include "signal_hunter.h"
#include "valve_controller.h"

namespace valve {

class Console {
 public:
  typedef void (*WriteFn)(void *ctx, const char *text);

  Console(Settings &settings, ValveController &controller, CanDecoder &decoder,
          SignalHunter &hunter, Mode &mode);

  void setWriter(WriteFn fn, void *ctx);
  void greet();

  // Feed received characters. Lines are dispatched on CR or LF.
  void feed(char c, uint32_t nowMs);
  void handleLine(const char *line, uint32_t nowMs);

  // Give every received frame to the console so it can service `sniff` and
  // `hunt`. Cheap when both are off.
  void onFrame(const CanFrame &frame, uint32_t nowMs);

  // Set by `save`; the platform layer clears it once the write succeeds.
  bool consumeSaveRequest();
  // Set by `mode`/`default`; lets the platform layer refresh the LED.
  bool consumeModeChanged();

  bool huntActive() const { return huntActive_; }
  bool sniffActive() const { return sniffActive_; }

  void print(const char *text);
  void printf(const char *fmt, ...);

 private:
  void cmdHelp();
  void cmdStatus();
  void cmdMode(int argc, char **argv);
  void cmdSet(int argc, char **argv);
  void cmdShow();
  void cmdSig(int argc, char **argv);
  void cmdTest(int argc, char **argv, uint32_t nowMs);
  void cmdHunt(int argc, char **argv);
  void cmdSniff(int argc, char **argv);
  void cmdCan();

  bool engineLikelyRunning() const;

  Settings &settings_;
  ValveController &controller_;
  CanDecoder &decoder_;
  SignalHunter &hunter_;
  Mode &mode_;

  WriteFn write_;
  void *ctx_;

  char line_[128];
  uint8_t lineLen_;

  bool saveRequested_;
  bool modeChanged_;

  bool huntActive_;
  bool sniffActive_;
  bool sniffFiltered_;
  uint32_t sniffId_;
  uint32_t sniffLastMs_;
};

}  // namespace valve
