#include "can_decode.h"

#include <string.h>

namespace valve {
namespace {

template <typename T>
T clampTo(float v, T lo, T hi) {
  if (v < static_cast<float>(lo)) return lo;
  if (v > static_cast<float>(hi)) return hi;
  return static_cast<T>(v);
}

}  // namespace

bool extractBits(const uint8_t *data, uint8_t dlc, uint8_t startBit,
                 uint8_t bitLength, Endian endian, uint32_t &raw) {
  if (data == nullptr) return false;
  if (bitLength == 0 || bitLength > 32) return false;
  if (dlc > 8) dlc = 8;

  raw = 0;

  if (endian == Endian::Little) {
    const uint16_t last = static_cast<uint16_t>(startBit) + bitLength - 1;
    if (last >= static_cast<uint16_t>(dlc) * 8u) return false;
    for (uint8_t i = 0; i < bitLength; ++i) {
      const uint16_t bit = static_cast<uint16_t>(startBit) + i;
      const uint8_t byte = static_cast<uint8_t>(bit >> 3);
      const uint8_t inByte = static_cast<uint8_t>(bit & 7);
      const uint32_t b = (data[byte] >> inByte) & 1u;
      raw |= b << i;
    }
    return true;
  }

  // Motorola/big endian: start at the most significant bit of the value and
  // walk down through the byte, hopping to the next byte when we fall off the
  // bottom of the current one.
  int16_t bit = static_cast<int16_t>(startBit);
  for (uint8_t i = 0; i < bitLength; ++i) {
    if (bit < 0) return false;
    const uint8_t byte = static_cast<uint8_t>(bit >> 3);
    if (byte >= dlc) return false;
    const uint8_t inByte = static_cast<uint8_t>(bit & 7);
    const uint32_t b = (data[byte] >> inByte) & 1u;
    raw = (raw << 1) | b;
    if (inByte == 0) {
      bit += 15;  // down one byte, back up to its most significant bit
    } else {
      bit -= 1;
    }
  }
  return true;
}

bool decodeSignal(const SignalDef &def, const CanFrame &frame, float &value) {
  if (!def.enabled) return false;
  if (def.canId != frame.id) return false;
  uint32_t raw = 0;
  if (!extractBits(frame.data, frame.dlc, def.startBit, def.bitLength,
                   def.endian, raw)) {
    return false;
  }
  value = static_cast<float>(raw) * def.scale + def.offset;
  return true;
}

CanDecoder::CanDecoder(const Settings &settings)
    : settings_(settings), frameCount_(0) {
  reset();
}

void CanDecoder::reset() {
  state_ = VehicleState();
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    lastSeenMs_[i] = 0;
    seen_[i] = false;
  }
  frameCount_ = 0;
}

bool CanDecoder::onFrame(const CanFrame &frame, uint32_t nowMs) {
  frameCount_++;
  bool matched = false;
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    float value = 0.0f;
    if (!decodeSignal(settings_.signals[i], frame, value)) continue;
    applySignal(static_cast<SignalId>(i), value, nowMs);
    matched = true;
  }
  if (matched) {
    state_.lastUpdateMs = nowMs;
  }
  return matched;
}

void CanDecoder::applySignal(SignalId id, float value, uint32_t nowMs) {
  const uint8_t idx = static_cast<uint8_t>(id);
  lastSeenMs_[idx] = nowMs;
  seen_[idx] = true;

  switch (id) {
    case SignalId::Rpm:
      state_.rpm = clampTo<uint16_t>(value, 0, 20000);
      state_.rpmValid = true;
      break;
    case SignalId::Pedal:
      state_.pedalPct = clampTo<uint8_t>(value, 0, 100);
      state_.pedalValid = true;
      break;
    case SignalId::Speed:
      state_.speedKph = clampTo<uint16_t>(value, 0, 500);
      state_.speedValid = true;
      break;
    case SignalId::Coolant:
      state_.coolantC = clampTo<int16_t>(value, -60, 250);
      state_.coolantValid = true;
      break;
    case SignalId::DriveSelect:
      state_.driveSelectRaw = clampTo<uint8_t>(value, 0, 255);
      state_.driveSelectValid = true;
      break;
    default:
      break;
  }
  state_.anyValid = true;
}

void CanDecoder::tick(uint32_t nowMs) {
  const uint32_t timeout = settings_.safety.signalTimeoutMs;
  bool any = false;
  for (uint8_t i = 0; i < kSignalCount; ++i) {
    if (!seen_[i]) continue;
    // Unsigned subtraction, so this stays correct across the millis() wrap.
    const bool stale = (nowMs - lastSeenMs_[i]) > timeout;
    if (!stale) {
      any = true;
      continue;
    }
    seen_[i] = false;
    switch (static_cast<SignalId>(i)) {
      case SignalId::Rpm: state_.rpmValid = false; state_.rpm = 0; break;
      case SignalId::Pedal: state_.pedalValid = false; state_.pedalPct = 0; break;
      case SignalId::Speed: state_.speedValid = false; state_.speedKph = 0; break;
      case SignalId::Coolant: state_.coolantValid = false; break;
      case SignalId::DriveSelect: state_.driveSelectValid = false; break;
      default: break;
    }
  }
  state_.anyValid = any;
}

}  // namespace valve
