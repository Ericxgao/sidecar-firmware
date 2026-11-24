#pragma once
#include <stdint.h>
namespace bridge {
inline uint32_t to_host_dropped() { return 0; }
inline bool samp_wants_voices() { return false; }
inline bool samp_wants_in() { return false; }
inline bool test_tap_on = false;
inline unsigned test_tap_blocks = 0;
inline int32_t test_tap[7][12] = {};
inline bool tap_on() { return test_tap_on; }
inline bool gr_live_on() { return false; }
inline bool livein_on() { return false; }
inline bool cal_on() { return false; }
inline void samp_voices(const int16_t (*)[8], unsigned, const int16_t (*)[16], const float (*)[4]) {}
inline void samp_in(const int32_t*, unsigned, unsigned) {}
inline void tap_in(const int32_t* p, unsigned frames, unsigned channels) {
  if (frames == 7 && channels == 12) {
    for (unsigned f = 0; f < frames; ++f)
      for (unsigned c = 0; c < channels; ++c) test_tap[f][c] = p[f * channels + c];
    ++test_tap_blocks;
  }
}
inline void gr_live_in(const int32_t*, unsigned, unsigned) {}
inline void livein_in(const int32_t*, unsigned, unsigned) {}
inline void cal_in(const int32_t*, unsigned, unsigned) {}
}
