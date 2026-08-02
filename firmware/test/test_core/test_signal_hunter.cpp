#include "signal_hunter.h"
#include "test_harness.h"

using namespace valve;

namespace {

// A frame that carries rpm at byte 2..3, little endian, 0.25 rpm per bit,
// with everything else constant so it cannot fit anything.
CanFrame rpmFrame(uint16_t rpm) {
  const uint16_t raw = static_cast<uint16_t>(rpm * 4);
  CanFrame f;
  f.id = 0x121;
  f.dlc = 8;
  f.data[0] = 0xAA;
  f.data[1] = 0x55;
  f.data[2] = static_cast<uint8_t>(raw & 0xFF);
  f.data[3] = static_cast<uint8_t>(raw >> 8);
  f.data[4] = 0x00;
  f.data[5] = 0xFF;
  f.data[6] = 0x10;
  f.data[7] = 0x20;
  return f;
}

CanFrame constantFrame(uint32_t id) {
  CanFrame f;
  f.id = id;
  f.dlc = 8;
  for (uint8_t i = 0; i < 8; ++i) f.data[i] = static_cast<uint8_t>(i * 7);
  return f;
}

void feed(SignalHunter &h, uint16_t rpm, int count = 20) {
  for (int i = 0; i < count; ++i) {
    h.onFrame(rpmFrame(rpm));
    h.onFrame(constantFrame(0x0AD));
  }
}

}  // namespace

void run_signal_hunter_tests() {
  printf("signal_hunter\n");

  TEST("the hunter finds a planted signal and derives its scale");
  {
    SignalHunter h;
    feed(h, 800);
    CHECK(h.mark(800));
    feed(h, 2000);
    CHECK(h.mark(2000));
    feed(h, 3500);
    CHECK(h.mark(3500));

    HuntResult results[5];
    const uint8_t n = h.rank(results, 5);
    CHECK(n > 0);
    if (n > 0) {
      CHECK_EQ(results[0].canId, 0x121u);
      CHECK_EQ(results[0].startBit, 16);
      CHECK_EQ(results[0].bitLength, 16);
      CHECK_EQ(static_cast<int>(results[0].endian),
               static_cast<int>(Endian::Little));
      CHECK_NEAR(results[0].scale, 0.25, 0.001);
      CHECK_NEAR(results[0].offset, 0.0, 0.5);
      CHECK(results[0].error < 1.0f);
    }
  }

  TEST("results come back best first");
  {
    SignalHunter h;
    feed(h, 900);
    h.mark(900);
    feed(h, 2400);
    h.mark(2400);
    feed(h, 4100);
    h.mark(4100);

    HuntResult results[8];
    const uint8_t n = h.rank(results, 8);
    CHECK(n >= 2);
    for (uint8_t i = 1; i < n; ++i) {
      CHECK(results[i - 1].error <= results[i].error);
    }
  }

  TEST("two marks are not enough to rank anything");
  {
    SignalHunter h;
    feed(h, 800);
    h.mark(800);
    feed(h, 3000);
    h.mark(3000);
    HuntResult results[5];
    CHECK_EQ(h.rank(results, 5), 0);
  }

  TEST("identical marks are rejected rather than fitted to noise");
  {
    SignalHunter h;
    feed(h, 1500);
    h.mark(1500);
    h.mark(1500);
    h.mark(1500);
    HuntResult results[5];
    CHECK_EQ(h.rank(results, 5), 0);
  }

  TEST("the mark table is bounded");
  {
    SignalHunter h;
    feed(h, 1000);
    for (uint8_t i = 0; i < kHunterMaxMarks; ++i) {
      CHECK(h.mark(1000.0f + i * 100));
    }
    CHECK(!h.mark(5000));
    CHECK_EQ(h.markCount(), kHunterMaxMarks);
  }

  TEST("candidate placements cover u8, u16 LE and u16 BE");
  {
    uint8_t startBit = 0, len = 0;
    Endian e = Endian::Little;

    SignalHunter::candidatePlacement(0, startBit, len, e);
    CHECK_EQ(startBit, 0);
    CHECK_EQ(len, 8);

    SignalHunter::candidatePlacement(7, startBit, len, e);
    CHECK_EQ(startBit, 56);
    CHECK_EQ(len, 8);

    SignalHunter::candidatePlacement(8, startBit, len, e);
    CHECK_EQ(startBit, 0);
    CHECK_EQ(len, 16);
    CHECK_EQ(static_cast<int>(e), static_cast<int>(Endian::Little));

    SignalHunter::candidatePlacement(15, startBit, len, e);
    CHECK_EQ(startBit, 7);
    CHECK_EQ(len, 16);
    CHECK_EQ(static_cast<int>(e), static_cast<int>(Endian::Big));

    SignalHunter::candidatePlacement(21, startBit, len, e);
    CHECK_EQ(startBit, 55);
    CHECK_EQ(static_cast<int>(e), static_cast<int>(Endian::Big));
  }

  TEST("running out of id slots is reported, not hidden");
  {
    SignalHunter h;
    for (uint32_t id = 0; id < kHunterMaxIds + 5u; ++id) {
      h.onFrame(constantFrame(0x200 + id));
    }
    CHECK_EQ(h.idCount(), kHunterMaxIds);
    CHECK(h.overflowed());
  }
}
