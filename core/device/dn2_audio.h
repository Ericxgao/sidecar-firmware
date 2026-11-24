// SPDX-License-Identifier: GPL-2.0-or-later
// Digitone II Overbridge 2.1 codec. No USB access or device registration here.
// Format reference: Overwitch 8b7b0a6, res/devices.json and src/engine.c.
// Independent integer implementation: signed 24-bit big-endian samples, the first
// 14 device->host fields padded to four bytes, remaining 28 packed to three.
// Host->device has eight padded fields. Hardware captures are still required.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../voice_gain.h"

namespace sidecar { namespace dn2 {
constexpr size_t kFrames = 7, kHeader = 32;
constexpr size_t kFromChannels = 42, kToChannels = 8;
constexpr size_t kFromFrameBytes = 140, kToFrameBytes = 32;
constexpr size_t kFromBlockBytes = kHeader + kFrames * kFromFrameBytes;
constexpr size_t kToBlockBytes = kHeader + kFrames * kToFrameBytes;
static_assert(kFromBlockBytes == 1012 && kToBlockBytes == 256, "DN2 block sizes");

// Values use Sidecar's signed Q31 full-scale convention, with eight zero low bits.
struct Input {
  uint16_t counter;
  int32_t samples[kFrames][kFromChannels];
};
struct Output {
  uint16_t counter;
  int32_t samples[kFrames][kToChannels];
};
enum class Result { Ok, Null, Size, Header };

inline int32_t read24(const uint8_t* p) {
  const uint32_t u = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8;
  int32_t v;
  memcpy(&v, &u, sizeof v); // bit transfer, no signed shift or unaligned load
  return v;
}
inline void write_padded24(uint8_t* p, int32_t v) {
  const uint32_t u = static_cast<uint32_t>(v);
  p[0] = v < 0 ? 0xff : 0;
  p[1] = uint8_t(u >> 24);
  p[2] = uint8_t(u >> 16);
  p[3] = uint8_t(u >> 8); // quantize toward negative infinity, like signed >> 8
}

// Single blocks only. A USB transfer containing several blocks must be split by
// the USB driver; refusing trailing bytes prevents silently losing a block.
// Errors leave the destination untouched. Header reserved bytes are not interpreted.
inline Result decode(const uint8_t* wire, size_t bytes, Input* out) {
  if (!wire || !out) return Result::Null;
  if (bytes != kFromBlockBytes) return Result::Size;
  if (wire[0] != 7 || wire[1] != 0) return Result::Header;
  out->counter = uint16_t(uint16_t(wire[2]) << 8 | wire[3]);
  const uint8_t* p = wire + kHeader;
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kFromChannels; ++c) {
      if (c < 14) ++p; // padding is not part of the 24-bit sample
      out->samples[f][c] = read24(p);
      p += 3;
    }
  return Result::Ok;
}

inline Result encode(const Output* in, uint8_t* wire, size_t bytes) {
  if (!wire || !in) return Result::Null;
  if (bytes != kToBlockBytes) return Result::Size;
  memset(wire, 0, kHeader);
  wire[0] = 7; wire[1] = 0xff;
  wire[2] = uint8_t(in->counter >> 8); wire[3] = uint8_t(in->counter);
  uint8_t* p = wire + kHeader;
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c, p += 4)
      write_padded24(p, in->samples[f][c]);
  return Result::Ok;
}

// Initial bring-up uses only MAIN L/R. Clear the six FX channels every block so
// uninitialized or previous data cannot leak into a native effects return.
inline void main_only(Output* out, uint16_t counter, const int32_t (*lr)[2]) {
  memset(out, 0, sizeof *out);
  out->counter = counter;
  if (lr) for (size_t f = 0; f < kFrames; ++f) {
    out->samples[f][0] = lr[f][0]; out->samples[f][1] = lr[f][1];
  }
}

// Native-processing mode: eight independent mono parts occupy the eight USB
// lanes. Input is Output::Pull's raw signed-16 voice tap, before the stereo sum
// and master FX. Undo only the legacy mixer headroom cut: these independent
// returns bypass the mixer/limiter it protects. Retain the engine scaling,
// pan ownership and native DN2 envelope. Saturate unexpected input safely.
// Call only after a matching DN2 DSP routing handshake; stock firmware interprets
// lanes 2..7 as FX returns. This helper alone does not enable that mode.
// Clear every inactive lane on every block, including after removal/disconnect.
inline void mono_parts(Output* out, uint16_t counter,
                       const int16_t (*parts)[kToChannels], uint8_t active) {
  memset(out, 0, sizeof *out);
  out->counter = counter;
  if (!parts) return;
  for (size_t f = 0; f < kFrames; ++f)
    for (size_t c = 0; c < kToChannels; ++c)
      if (active & (1u << c)) {
        const int64_t sample = int64_t(parts[f][c]) * 65536 * 32768 / core::kVoiceGainQ15;
        out->samples[f][c] = sample > INT32_MAX ? INT32_MAX :
                            sample < INT32_MIN ? INT32_MIN : int32_t(sample);
      }
}

// Diagnostic adaptation to Sidecar's current 12-channel input consumers:
// main stereo, first eight native DN2 tracks downmixed to mono, external input.
// These are DN2 stems, NOT the Pi's own voice outputs. Pi voice meters/recording
// require separate engine taps. Remaining tracks and FX remain in Input above.
inline void monitor12(const Input& in, int32_t (*out)[12]) {
  for (size_t f = 0; f < kFrames; ++f) {
    out[f][0] = in.samples[f][0]; out[f][1] = in.samples[f][1];
    for (size_t t = 0; t < 8; ++t)
      out[f][2 + t] = int32_t((int64_t(in.samples[f][2 + t * 2]) + in.samples[f][3 + t * 2]) / 2);
    out[f][10] = in.samples[f][40]; out[f][11] = in.samples[f][41];
  }
}
inline uint16_t next_counter(uint16_t counter) { return uint16_t(counter + kFrames); }
}} // namespace sidecar::dn2
