// core/tests/test_voice_levels.cpp - the voices' levels: LEV 0 is silence on DAISYSP (no 0.05 floor).
#include <cmath>
#include <cstdio>
#include <vector>

#include "check.h"
#include "voices.h"

using namespace core;

static std::vector<uint8_t> report(int vb, int note, int vel, std::vector<float> p) {
  std::vector<uint8_t> m = {0xf0, 0x7d, 0x52, 0x54, 0x04, (uint8_t)vb, (uint8_t)note, (uint8_t)vel};
  p.resize(16, 0);
  for (float x : p) { int i = (int)x; m.push_back(i); m.push_back((int)((x - i) * 128) & 0x7f); }
  m.push_back(0xf7);
  return m;
}

static int16_t ring[kBlock * 64][kVoices];
// one trig on voice 2, blocks rendered; the voice row's peak from block `from` on (int16 counts)
static int peak_of(int vb, int vel, const std::vector<float>& p, int blocks, int from = 0) {
  auto t = report(vb | 2, 48, vel, p);
  voice_report(0x04, t.data(), (int)t.size());
  uint64_t mach[kMachines] = {0};
  int pk = 0;
  for (int b = 0; b < blocks; ++b) {
    render_voices(ring, kBlock * 64, (b % 64) * kBlock, 0, kVoices, mach);
    if (b < from) continue;
    for (int s = 0; s < kBlock; ++s) {
      int a = std::abs((int)ring[(b % 64) * kBlock + s][2]);
      pk = a > pk ? a : pk;
    }
  }
  voice_silence(2);
  for (int b = 0; b < 400; ++b) render_voices(ring, kBlock * 64, (b % 64) * kBlock, 0, kVoices, mach);
  return pk;
}
static double db(int a, int b) { return 20 * std::log10((double)a / b); }

int main() {
  hooks.to_rytm = [](const uint8_t*, int) {};
  voices_init();
  const int kSec = 4000;   // blocks in a second
  // DAISYSP STRING (ENG zone 1 of 3), vel 127. DMP 0 (the longest ring): LEV 0 silent after the attack (< -90 dBFS;
  // the old 0.05 floor left it at -27 dBFS, still ringing). DMP 64: LEV 32 6 to 12 dB under LEV 64 (STRING's strike
  // scales by the level twice, its string once: daisy_engine.h)
  {
    auto P = [](float lev, float dmp) { return std::vector<float>{64, 64, 64, dmp, 0, 64, 64, lev, 64, 64, 0, 0, 0, 0, 0, 0}; };
    int z = peak_of(kDaisy, 127, P(0, 0), kSec, 200), hi = peak_of(kDaisy, 127, P(64, 64), kSec), lo = peak_of(kDaisy, 127, P(32, 64), kSec);
    printf("DAISYSP STRING: LEV 0 %d; DMP 64: LEV 64 %d, 32 %d\n", z, hi, lo);
    CHECK(z < 1);   // -90 dBFS is 1.04 counts
    CHECK(db(lo, hi) < -6.0 && db(lo, hi) > -12.0);
  }
  printf("test_voice_levels: PASS\n");
  return 0;
}
