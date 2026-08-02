#include "can_decode.h"
#include "test_harness.h"

using namespace valve;

namespace {

CanFrame makeFrame(uint32_t id, uint8_t dlc, const uint8_t *bytes) {
  CanFrame f;
  f.id = id;
  f.dlc = dlc;
  for (uint8_t i = 0; i < 8; ++i) f.data[i] = i < dlc ? bytes[i] : 0;
  return f;
}

}  // namespace

void run_can_decode_tests() {
  printf("can_decode\n");

  TEST("little endian extraction spans bytes");
  {
    const uint8_t bytes[8] = {0x34, 0x12, 0, 0, 0, 0, 0, 0};
    uint32_t raw = 0;
    CHECK(extractBits(bytes, 8, 0, 16, Endian::Little, raw));
    CHECK_EQ(raw, 0x1234u);
  }

  TEST("big endian extraction spans bytes");
  {
    const uint8_t bytes[8] = {0x12, 0x34, 0, 0, 0, 0, 0, 0};
    uint32_t raw = 0;
    // Start bit 7 is the most significant bit of byte 0.
    CHECK(extractBits(bytes, 8, 7, 16, Endian::Big, raw));
    CHECK_EQ(raw, 0x1234u);
  }

  TEST("sub-byte fields respect bit offset");
  {
    const uint8_t bytes[8] = {0b11010000, 0, 0, 0, 0, 0, 0, 0};
    uint32_t raw = 0;
    CHECK(extractBits(bytes, 8, 4, 4, Endian::Little, raw));
    CHECK_EQ(raw, 0b1101u);
  }

  TEST("fields past the DLC are rejected");
  {
    const uint8_t bytes[8] = {1, 2, 3, 4, 0, 0, 0, 0};
    uint32_t raw = 0;
    CHECK(!extractBits(bytes, 4, 24, 16, Endian::Little, raw));
    CHECK(!extractBits(bytes, 4, 31, 16, Endian::Big, raw));
    CHECK(extractBits(bytes, 4, 16, 16, Endian::Little, raw));
  }

  TEST("zero and oversize lengths are rejected");
  {
    const uint8_t bytes[8] = {0};
    uint32_t raw = 0;
    CHECK(!extractBits(bytes, 8, 0, 0, Endian::Little, raw));
    CHECK(!extractBits(bytes, 8, 0, 33, Endian::Little, raw));
  }

  TEST("scale and offset are applied");
  {
    SignalDef def;
    def.canId = 0x100;
    def.startBit = 0;
    def.bitLength = 16;
    def.endian = Endian::Little;
    def.scale = 0.25f;
    def.offset = 0.0f;
    def.enabled = true;

    const uint8_t bytes[8] = {0x00, 0x20, 0, 0, 0, 0, 0, 0};  // 0x2000 = 8192
    CanFrame f = makeFrame(0x100, 8, bytes);
    float v = 0.0f;
    CHECK(decodeSignal(def, f, v));
    CHECK_NEAR(v, 2048.0, 0.001);

    f.id = 0x101;
    CHECK(!decodeSignal(def, f, v));

    def.enabled = false;
    f.id = 0x100;
    CHECK(!decodeSignal(def, f, v));
  }

  TEST("decoder populates state and ages it out");
  {
    Settings s;
    loadDefaults(s);
    s.safety.signalTimeoutMs = 500;
    SignalDef &rpm = s.signals[static_cast<uint8_t>(SignalId::Rpm)];
    rpm.canId = 0x100;
    rpm.startBit = 0;
    rpm.bitLength = 16;
    rpm.endian = Endian::Little;
    rpm.scale = 0.25f;
    rpm.offset = 0.0f;
    rpm.enabled = true;

    SignalDef &coolant = s.signals[static_cast<uint8_t>(SignalId::Coolant)];
    coolant.canId = 0x100;
    coolant.startBit = 16;
    coolant.bitLength = 8;
    coolant.endian = Endian::Little;
    coolant.scale = 0.75f;
    coolant.offset = -48.0f;
    coolant.enabled = true;

    CanDecoder dec(s);
    const uint8_t bytes[8] = {0x00, 0x20, 0x80, 0, 0, 0, 0, 0};
    CHECK(dec.onFrame(makeFrame(0x100, 8, bytes), 1000));
    CHECK(dec.state().rpmValid);
    CHECK_EQ(dec.state().rpm, 2048);
    CHECK(dec.state().coolantValid);
    CHECK_EQ(dec.state().coolantC, 48);  // 128 * 0.75 - 48
    CHECK(dec.state().anyValid);

    dec.tick(1400);
    CHECK(dec.state().rpmValid);

    dec.tick(1600);
    CHECK(!dec.state().rpmValid);
    CHECK(!dec.state().coolantValid);
    CHECK(!dec.state().anyValid);
    CHECK_EQ(dec.state().rpm, 0);
  }

  TEST("unmatched frames are counted but change nothing");
  {
    Settings s;
    loadDefaults(s);
    CanDecoder dec(s);
    const uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!dec.onFrame(makeFrame(0x123, 8, bytes), 10));
    CHECK_EQ(dec.frameCount(), 1u);
    CHECK(!dec.state().anyValid);
  }

  TEST("staleness survives the millis wrap");
  {
    Settings s;
    loadDefaults(s);
    s.safety.signalTimeoutMs = 500;
    SignalDef &rpm = s.signals[static_cast<uint8_t>(SignalId::Rpm)];
    rpm.canId = 0x100;
    rpm.bitLength = 16;
    rpm.scale = 1.0f;
    rpm.enabled = true;

    CanDecoder dec(s);
    const uint8_t bytes[8] = {0x10, 0x00, 0, 0, 0, 0, 0, 0};
    const uint32_t nearMax = 0xFFFFFF00u;
    dec.onFrame(makeFrame(0x100, 8, bytes), nearMax);
    // 0x100 ms later, having wrapped through zero.
    dec.tick(nearMax + 0x100);
    CHECK(dec.state().rpmValid);
    dec.tick(nearMax + 600);
    CHECK(!dec.state().rpmValid);
  }
}
