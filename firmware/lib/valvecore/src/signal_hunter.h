// Finds where a quantity lives on the bus, on the actual car.
//
// Bit placements on the MQB powertrain bus vary with model year and build, and
// a controller that ships with somebody else's numbers hard-coded is a
// controller that silently does the wrong thing. So instead of guessing, this
// watches every frame, keeps every plausible 8- and 16-bit field as a
// candidate, and asks the installer to mark a few known operating points
// ("I am sitting at exactly 2000 rpm"). It then least-squares each candidate
// against those marks and reports the ones that fit, complete with the scale
// and offset to paste into `sig`.
#pragma once

#include "can_decode.h"
#include "valve_types.h"

namespace valve {

static const uint8_t kHunterMaxIds = 40;
static const uint8_t kHunterCandidates = 22;  // 8 x u8, 7 x u16 LE, 7 x u16 BE
static const uint8_t kHunterMaxMarks = 4;
static const uint8_t kHunterMinMarks = 3;

struct HuntResult {
  uint32_t canId;
  uint8_t startBit;
  uint8_t bitLength;
  Endian endian;
  float scale;
  float offset;
  // Worst deviation between the fitted value and what the installer declared,
  // in engineering units. Small is good.
  float error;
};

class SignalHunter {
 public:
  SignalHunter();

  void reset();

  void onFrame(const CanFrame &frame);

  // Snapshot every candidate against a known true value. Returns false when
  // the mark table is full.
  bool mark(float trueValue);

  // Fill `out` with up to `maxOut` best-fitting candidates, best first.
  // Returns how many were written. Needs at least kHunterMinMarks marks.
  uint8_t rank(HuntResult *out, uint8_t maxOut) const;

  uint8_t markCount() const { return markCount_; }
  uint8_t idCount() const { return idCount_; }
  uint32_t frameCount() const { return frameCount_; }
  bool overflowed() const { return overflowed_; }

  // Decodes a candidate index into its placement. Exposed for tests.
  static void candidatePlacement(uint8_t index, uint8_t &startBit,
                                 uint8_t &bitLength, Endian &endian);

 private:
  struct IdSlot {
    uint32_t canId;
    uint16_t current[kHunterCandidates];
    bool currentValid[kHunterCandidates];
    uint16_t marks[kHunterMaxMarks][kHunterCandidates];
    bool markValid[kHunterMaxMarks][kHunterCandidates];
  };

  IdSlot *findOrCreate(uint32_t canId);

  IdSlot slots_[kHunterMaxIds];
  uint8_t idCount_;
  float markValues_[kHunterMaxMarks];
  uint8_t markCount_;
  uint32_t frameCount_;
  bool overflowed_;
};

}  // namespace valve
