// pi/engine/lagdiag.h - lag diagnostics for the black box (notes from the Rytm-lag hunt; docs/pi-engine.md "Lag diagnostics").
// Portable, header only, no allocation (tests/test_lagdiag.cpp). Three parts, each one small class and a formatter:
//   1 the "Lag now" marker (Marker, Snap): the tester presses a button when the Rytm feels slow; one black box line says when,
//     and one more holds the per-second counters as they stood
//   2 the Rytm's report stream (RxDiag): how the Rytm's own 03 / 04 reports arrive
//   3 what the Pi sends the Rytm (TxDiag)
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if __has_include(<circle/memorymap.h>)   // the kernel: no snprintf, platform_pi.cpp's subset (%u %02u %s)
extern "C" int core_snprintf(char* out, size_t n, const char* fmt, ...);
#define LAGDIAG_FMT core_snprintf
#else
#include <stdio.h>
#define LAGDIAG_FMT snprintf
#endif

namespace lagdiag {

// ---- 1. the marker -------------------------------------------------------------------------------------------------

// The console command for it: exactly this, no argument. A client can not put text into the log with it.
inline bool is_marker_cmd(const char* c) { return c != nullptr && strcmp(c, "lagnow") == 0; }

// One marker a second at most (a button held down or double-tapped writes one line); N counts the accepted ones per boot.
class Marker {
 public:
  static constexpr uint32_t kMinMs = 1000;
  // now_ms: any 32-bit millisecond clock (it may wrap). The new N, or 0 when this press came too soon after the last.
  unsigned press(uint32_t now_ms) {
    if (n_ != 0 && (uint32_t)(now_ms - last_ms_) < kMinMs) {
      ++refused_;
      return 0;
    }
    last_ms_ = now_ms;
    return ++n_;
  }
  unsigned count() const { return n_; }
  unsigned refused() const { return refused_; }

 private:
  unsigned n_ = 0, refused_ = 0;
  uint32_t last_ms_ = 0;
};

inline unsigned cut(char* out, unsigned max, int w) {   // snprintf's result as the length really written
  if (max == 0) return 0;
  if (w < 0) w = 0, out[0] = 0;
  return (unsigned)w < max ? (unsigned)w : max - 1;
}

// "lag-marker N at HH:MM:SS.cc": the same uptime clock as the log's own time stamps
inline unsigned fmt_marker(char* out, unsigned max, unsigned n, uint32_t uptime_ms) {
  if (max == 0) return 0;
  uint32_t cs = uptime_ms / 10, s = cs / 100;
  return cut(out, max, LAGDIAG_FMT(out, max, "lag-marker %u at %02u:%02u:%02u.%02u", n, (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                                (unsigned)(s % 60), (unsigned)(cs % 100)));
}

constexpr unsigned kLine = 200;   // a diagnostic line's text at most (the log's own line limit is far above it)

// The engine: line's figures as they stood at the last second (kernel.cpp LogStats fills it), and the last lines of parts 2 and 3
struct Snap {
  unsigned fifo_min, under, block_max_us;
  unsigned c1, c2, c3;   // the cores' busy share, tenths of a percent
  unsigned pass_max_us, midi_bytes, sysex;
  unsigned sent_bytes, sent_msgs, sent_fail, dropped;
  char rx[kLine], tx[kLine];   // the last rxrep / txacc line text ("" none)
};

inline unsigned fmt_snap(char* out, unsigned max, const Snap& s) {
  if (max == 0) return 0;
  int w = LAGDIAG_FMT(out, max,
                   "lag-snap: fifo min %u under %u | block max %u us | cpu c1 %u.%u c2 %u.%u c3 %u.%u %% | pass max %u us | "
                   "midi %u B sysex %u | to-host %u B %u msgs fail %u dropped %u",
                   s.fifo_min, s.under, s.block_max_us, s.c1 / 10, s.c1 % 10, s.c2 / 10, s.c2 % 10, s.c3 / 10, s.c3 % 10, s.pass_max_us,
                   s.midi_bytes, s.sysex, s.sent_bytes, s.sent_msgs, s.sent_fail, s.dropped);
  unsigned n = cut(out, max, w);
  if (s.rx[0] && n + 1 < max) n += cut(out + n, max - n, LAGDIAG_FMT(out + n, max - n, " | rx: %s", s.rx));
  if (s.tx[0] && n + 1 < max) n += cut(out + n, max - n, LAGDIAG_FMT(out + n, max - n, " | tx: %s", s.tx));
  return n;
}

}   // namespace lagdiag
