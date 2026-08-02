#include "signal_hunter.h"

#include <math.h>
#include <string.h>

namespace valve {

SignalHunter::SignalHunter() { reset(); }

void SignalHunter::reset() {
  memset(slots_, 0, sizeof(slots_));
  idCount_ = 0;
  markCount_ = 0;
  frameCount_ = 0;
  overflowed_ = false;
  for (uint8_t i = 0; i < kHunterMaxMarks; ++i) markValues_[i] = 0.0f;
}

void SignalHunter::candidatePlacement(uint8_t index, uint8_t &startBit,
                                      uint8_t &bitLength, Endian &endian) {
  if (index < 8) {
    startBit = static_cast<uint8_t>(index * 8);
    bitLength = 8;
    endian = Endian::Little;
  } else if (index < 15) {
    startBit = static_cast<uint8_t>((index - 8) * 8);
    bitLength = 16;
    endian = Endian::Little;
  } else {
    // Big endian 16-bit starting at the most significant bit of the byte.
    startBit = static_cast<uint8_t>((index - 15) * 8 + 7);
    bitLength = 16;
    endian = Endian::Big;
  }
}

SignalHunter::IdSlot *SignalHunter::findOrCreate(uint32_t canId) {
  for (uint8_t i = 0; i < idCount_; ++i) {
    if (slots_[i].canId == canId) return &slots_[i];
  }
  if (idCount_ >= kHunterMaxIds) {
    overflowed_ = true;
    return nullptr;
  }
  IdSlot *slot = &slots_[idCount_++];
  memset(slot, 0, sizeof(*slot));
  slot->canId = canId;
  return slot;
}

void SignalHunter::onFrame(const CanFrame &frame) {
  frameCount_++;
  IdSlot *slot = findOrCreate(frame.id);
  if (slot == nullptr) return;

  for (uint8_t c = 0; c < kHunterCandidates; ++c) {
    uint8_t startBit, bitLength;
    Endian endian;
    candidatePlacement(c, startBit, bitLength, endian);
    uint32_t raw = 0;
    if (extractBits(frame.data, frame.dlc, startBit, bitLength, endian, raw)) {
      slot->current[c] = static_cast<uint16_t>(raw);
      slot->currentValid[c] = true;
    } else {
      slot->currentValid[c] = false;
    }
  }
}

bool SignalHunter::mark(float trueValue) {
  if (markCount_ >= kHunterMaxMarks) return false;
  const uint8_t m = markCount_;
  markValues_[m] = trueValue;
  for (uint8_t i = 0; i < idCount_; ++i) {
    for (uint8_t c = 0; c < kHunterCandidates; ++c) {
      slots_[i].marks[m][c] = slots_[i].current[c];
      slots_[i].markValid[m][c] = slots_[i].currentValid[c];
    }
  }
  markCount_++;
  return true;
}

uint8_t SignalHunter::rank(HuntResult *out, uint8_t maxOut) const {
  if (out == nullptr || maxOut == 0) return 0;
  if (markCount_ < kHunterMinMarks) return 0;

  // Precompute the statistics of the declared values, which are shared by
  // every candidate.
  float sumX = 0.0f, sumXX = 0.0f;
  for (uint8_t m = 0; m < markCount_; ++m) {
    sumX += markValues_[m];
    sumXX += markValues_[m] * markValues_[m];
  }
  const float n = static_cast<float>(markCount_);
  const float denom = n * sumXX - sumX * sumX;
  if (fabsf(denom) < 1e-6f) return 0;  // installer gave identical marks

  uint8_t count = 0;
  for (uint8_t i = 0; i < idCount_; ++i) {
    const IdSlot &slot = slots_[i];
    for (uint8_t c = 0; c < kHunterCandidates; ++c) {
      bool valid = true;
      for (uint8_t m = 0; m < markCount_; ++m) {
        if (!slot.markValid[m][c]) { valid = false; break; }
      }
      if (!valid) continue;

      // Least squares fit raw = a * declared + b.
      float sumY = 0.0f, sumXY = 0.0f;
      for (uint8_t m = 0; m < markCount_; ++m) {
        const float y = static_cast<float>(slot.marks[m][c]);
        sumY += y;
        sumXY += markValues_[m] * y;
      }
      const float a = (n * sumXY - sumX * sumY) / denom;
      const float b = (sumY - a * sumX) / n;
      // A field that never moved cannot be what we are looking for.
      if (fabsf(a) < 1e-4f) continue;

      float worst = 0.0f;
      for (uint8_t m = 0; m < markCount_; ++m) {
        const float fitted = a * markValues_[m] + b;
        const float residual = static_cast<float>(slot.marks[m][c]) - fitted;
        // Express the miss in engineering units so the number means something.
        const float inUnits = fabsf(residual / a);
        if (inUnits > worst) worst = inUnits;
      }

      HuntResult r;
      r.canId = slot.canId;
      candidatePlacement(c, r.startBit, r.bitLength, r.endian);
      r.scale = 1.0f / a;
      r.offset = -b / a;
      r.error = worst;

      // Insertion sort into the caller's top-N buffer.
      uint8_t pos = count < maxOut ? count : maxOut;
      while (pos > 0 && out[pos - 1].error > r.error) {
        if (pos < maxOut) out[pos] = out[pos - 1];
        pos--;
      }
      if (pos < maxOut) {
        out[pos] = r;
        if (count < maxOut) count++;
      }
    }
  }
  return count;
}

}  // namespace valve
