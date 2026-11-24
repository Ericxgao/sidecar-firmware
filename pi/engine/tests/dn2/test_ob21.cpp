// SPDX-License-Identifier: GPL-2.0-or-later
#include "ob21.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
using namespace sidecar::dn2;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static int32_t q31(int32_t q23) { return int32_t(int64_t(q23) * 256); }
// Reference fixtures use arithmetic and explicit byte positions, not the codec.
static int32_t sample(size_t f, size_t c) {
  const int32_t edge[] = {0, 1, -1, 8388607, -8388608, 4194304, -4194304};
  if (c < 7) return edge[(c + f) % 7];
  return int32_t(f * 65537 + c * 1777) * ((c & 1) ? -1 : 1);
}
static void input_fixture(uint8_t* wire) {
  memset(wire, 0, kFromBlockBytes);
  wire[0] = 7; wire[2] = 0xff; wire[3] = 0xfc;
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kFromChannels; ++c) {
      const size_t offset = kHeader + f * 140 + (c < 14 ? c * 4 + 1 : 56 + (c - 14) * 3);
      const uint32_t u = uint32_t(sample(f, c)) & 0xffffff;
      if (c < 14) wire[offset - 1] = 0xa5; // deliberately noncanonical padding
      wire[offset] = uint8_t(u / 65536);
      wire[offset + 1] = uint8_t(u / 256);
      wire[offset + 2] = uint8_t(u);
    }
}
int main() {
  std::array<uint8_t, kFromBlockBytes + 2> guarded{};
  guarded.front() = 0x12; guarded.back() = 0x34;
  uint8_t* wire = guarded.data() + 1; // deliberately unaligned
  input_fixture(wire);
  Input in{};
  CHECK(decode(wire, kFromBlockBytes, &in) == Result::Ok);
  CHECK(in.counter == 65532 && next_counter(in.counter) == 3);
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kFromChannels; ++c) CHECK(in.samples[f][c] == q31(sample(f, c)));
  CHECK(guarded.front() == 0x12 && guarded.back() == 0x34);
  Input saved = in;
  for (size_t n = 0; n < kFromBlockBytes; ++n) {
    CHECK(decode(wire, n, &in) == Result::Size);
    CHECK(memcmp(&in, &saved, sizeof in) == 0);
  }
  CHECK(decode(wire, kFromBlockBytes + 1, &in) == Result::Size);
  wire[1] = 0xff; CHECK(decode(wire, kFromBlockBytes, &in) == Result::Header);
  CHECK(memcmp(&in, &saved, sizeof in) == 0); wire[1] = 0;
  CHECK(decode(nullptr, kFromBlockBytes, &in) == Result::Null);
  CHECK(decode(wire, kFromBlockBytes, nullptr) == Result::Null);

  int32_t monitor[kFrames][12]; monitor12(in, monitor);
  for (size_t f = 0; f < kFrames; ++f) {
    CHECK(monitor[f][0] == q31(sample(f, 0)) && monitor[f][1] == q31(sample(f, 1)));
    CHECK(monitor[f][10] == q31(sample(f, 40)) && monitor[f][11] == q31(sample(f, 41)));
    for (size_t t = 0; t < 8; ++t)
      CHECK(monitor[f][t + 2] == int32_t((int64_t(sample(f, 2 + t * 2)) + sample(f, 3 + t * 2)) * 128));
  }
  in.samples[0][2] = INT32_MAX; in.samples[0][3] = INT32_MAX;
  in.samples[1][2] = INT32_MIN; in.samples[1][3] = INT32_MIN;
  monitor12(in, monitor);
  CHECK(monitor[0][2] == INT32_MAX && monitor[1][2] == INT32_MIN);

  Output out{}; out.counter = 0x1234;
  const int32_t values[] = {0, 1, -1, INT32_MAX, INT32_MIN, 0x40000000, -0x40000000, 257};
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c) out.samples[f][c] = values[(f + c) % 8];
  std::array<uint8_t, kToBlockBytes + 2> encoded{};
  encoded.front() = 0x56; encoded.back() = 0x78;
  uint8_t* dst = encoded.data() + 1;
  CHECK(encode(&out, dst, kToBlockBytes) == Result::Ok);
  CHECK(dst[0] == 7 && dst[1] == 255 && dst[2] == 0x12 && dst[3] == 0x34);
  for (size_t i = 4; i < 32; ++i) CHECK(dst[i] == 0);
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c) {
      const int64_t v = out.samples[f][c];
      const int64_t quantized = v >= 0 ? v / 256 : -((-v + 255) / 256);
      const uint8_t* p = dst + 32 + f * 32 + c * 4;
      int64_t word = int64_t(p[0]) * 16777216 + int64_t(p[1]) * 65536 + int64_t(p[2]) * 256 + p[3];
      if (word >= 2147483648LL) word -= 4294967296LL;
      CHECK(word == quantized);
    }
  CHECK(encoded.front() == 0x56 && encoded.back() == 0x78);
  const auto encoded_saved = encoded;
  for (size_t n = 0; n < kToBlockBytes; ++n) CHECK(encode(&out, dst, n) == Result::Size);
  CHECK(encode(&out, dst, kToBlockBytes + 1) == Result::Size);
  CHECK(encoded == encoded_saved);
  CHECK(encode(nullptr, dst, kToBlockBytes) == Result::Null);
  CHECK(encode(&out, nullptr, kToBlockBytes) == Result::Null);

  int32_t lr[kFrames][2];
  for (size_t f = 0; f < kFrames; ++f) lr[f][0] = values[f], lr[f][1] = values[f + 1];
  main_only(&out, 99, lr);
  CHECK(out.counter == 99);
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c) CHECK(out.samples[f][c] == (c < 2 ? lr[f][c] : 0));
  main_only(&out, 100, nullptr);
  for (const auto& frame : out.samples) for (int32_t s : frame) CHECK(s == 0);

  // Distinct signals on every lane must survive the full USB packet encoding,
  // including negative full scale; each inactive lane must clear previous audio.
  int16_t parts[kFrames][kToChannels];
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c)
      parts[f][c] = int16_t((int(f) * 1901 + int(c) * 3571) - 16000);
  parts[0][0] = INT16_MIN; parts[0][7] = INT16_MAX;
  for (unsigned mask : {255u, 85u, 170u, 128u, 1u, 0u}) {
    mono_parts(&out, 65535, parts, uint8_t(mask));
    CHECK(encode(&out, dst, kToBlockBytes) == Result::Ok);
    CHECK(dst[2] == 255 && dst[3] == 255);
    for (size_t f = 0; f < kFrames; ++f)
      for (size_t c = 0; c < kToChannels; ++c) {
        const uint8_t* p = dst + 32 + f * 32 + c * 4;
        int64_t word = int64_t(p[0]) * 16777216 + int64_t(p[1]) * 65536 + int64_t(p[2]) * 256 + p[3];
        if (word >= 2147483648LL) word -= 4294967296LL;
        // Independent floating reference includes the post-mixer gain recovery.
        double expected = double(parts[f][c]) * 256.0 / (11469.0 / 32768.0);
        if (expected > 8388607) expected = 8388607;
        if (expected < -8388608) expected = -8388608;
        if (!(mask & (1u << c))) expected = 0;
        CHECK(word - expected < 1.01 && expected - word < 1.01);
      }
    CHECK(encoded.front() == 0x56 && encoded.back() == 0x78);
  }
  // Every possible source word: monotonic, bounded and within one transport LSB
  // of an independent double reference, including invalid over-range taps.
  int32_t previous = INT32_MIN;
  for (int input = INT16_MIN; input <= INT16_MAX; ++input) {
    parts[0][0] = int16_t(input);
    mono_parts(&out, 0, parts, 1);
    const int32_t actual = out.samples[0][0];
    double expected = double(input) * 65536.0 / (11469.0 / 32768.0);
    if (expected > INT32_MAX) expected = INT32_MAX;
    if (expected < INT32_MIN) expected = INT32_MIN;
    CHECK(actual >= previous);
    CHECK(double(actual) - expected < 1.01 && expected - double(actual) < 1.01);
    previous = actual;
  }
  mono_parts(&out, 0, nullptr, 255);
  for (const auto& frame : out.samples) for (int32_t s : frame) CHECK(s == 0);
  std::puts("PASS: eight independent native mono lanes, signed scaling, inactive-lane and missing-source silence");
  std::puts("PASS: all 42 input channels, 8 outputs, Q31 scaling, boundaries, malformed sizes/headers, counter wrap, monitor mapping, main-only silence");
}
