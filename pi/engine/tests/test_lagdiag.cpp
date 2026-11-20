// pi/engine/tests/test_lagdiag.cpp - the lag diagnostics (lagdiag.h): the "Lag now" marker and its snapshot (part 1),
// the Rytm report-stream timing (part 2) and the Pi-to-Rytm accounting (part 3).
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "lagdiag.h"

static void marker_tests() {
  // the command: exactly "lagnow", never an argument (no free text from the client reaches the log)
  CHECK(lagdiag::is_marker_cmd("lagnow"));
  CHECK(!lagdiag::is_marker_cmd("lagnow=hello"));
  CHECK(!lagdiag::is_marker_cmd("lagnow "));
  CHECK(!lagdiag::is_marker_cmd("lagno"));
  CHECK(!lagdiag::is_marker_cmd(""));
  CHECK(!lagdiag::is_marker_cmd(nullptr));

  // one marker a second at most; N counts the accepted ones per boot
  lagdiag::Marker m;
  CHECK(m.press(5000) == 1);
  CHECK(m.press(5400) == 0);   // 400 ms later: refused, not counted
  CHECK(m.press(5999) == 0);
  CHECK(m.refused() == 2);
  CHECK(m.press(6000) == 2);   // a second after the first accepted one
  CHECK(m.press(7500) == 3);
  // the 32-bit millisecond clock wraps: 1 s still counts across it
  lagdiag::Marker w;
  CHECK(w.press(0xFFFFFF00u) == 1);
  CHECK(w.press(0x000000FFu) == 0);   // 510 ms later
  CHECK(w.press(0x00000300u) == 2);   // 1.0 s later

  char b[96];
  lagdiag::fmt_marker(b, sizeof b, 2, 3723450);   // 1 h 2 min 3.45 s, in ms
  CHECK(std::string(b) == "lag-marker 2 at 01:02:03.45");
  lagdiag::fmt_marker(b, sizeof b, 17, 59999);
  CHECK(std::string(b) == "lag-marker 17 at 00:00:59.99");
  lagdiag::fmt_marker(b, 12, 1, 0);   // a short buffer: cut, terminated, never over
  CHECK(strlen(b) == 11);

  // the one-shot snapshot of the per-second counters (the engine: line's numbers, and the last rxrep / tx lines)
  lagdiag::Snap s = {};
  s.fifo_min = 71, s.under = 411, s.block_max_us = 17, s.c1 = 53, s.c2 = 23, s.c3 = 119, s.pass_max_us = 121;
  s.midi_bytes = 8391, s.sysex = 103262, s.sent_bytes = 545408, s.sent_msgs = 20631, s.sent_fail = 0, s.dropped = 0;
  strcpy(s.rx, "rxrep: v5 n94 g10700 m0003");
  strcpy(s.tx, "txacc: ka n1 b6");
  lagdiag::fmt_snap(b, 0, s);   // (no room: nothing written, no crash)
  char big[400];
  unsigned n = lagdiag::fmt_snap(big, sizeof big, s);
  CHECK(n == strlen(big));
  CHECK(std::string(big) ==
        "lag-snap: fifo min 71 under 411 | block max 17 us | cpu c1 5.3 c2 2.3 c3 11.9 % | pass max 121 us | "
        "midi 8391 B sysex 103262 | to-host 545408 B 20631 msgs fail 0 dropped 0 | rx: rxrep: v5 n94 g10700 m0003 | tx: txacc: ka n1 b6");
  s.rx[0] = s.tx[0] = 0;   // no stream, nothing sent lately: those parts left out
  lagdiag::fmt_snap(big, sizeof big, s);
  CHECK(std::string(big).find("rx:") == std::string::npos && std::string(big).find("tx:") == std::string::npos);
  n = lagdiag::fmt_snap(big, 40, s);   // cut to the buffer
  CHECK(n == 39 && strlen(big) == 39);
}

// ---- part 2: the Rytm's report stream ---------------------------------------------------------------------------------

// A Rytm 03 (PARAMS, 41 bytes with the tag; short: 23) or 04 (TRIG+PARAMS, 43; short: 25) report for voice v: F0 7D 52 54 cmd
// (machine | v) [note vel for 04] then P1..P16 (P1..P8) as 7-bit pairs, [tag pair], F7
static std::vector<uint8_t> report(uint8_t cmd, unsigned v, const std::vector<unsigned>& words, bool tag = true) {
  std::vector<uint8_t> r = {0xF0, 0x7D, 0x52, 0x54, cmd, (uint8_t)(0x08 | v)};
  if (cmd == 0x04 || cmd == 0x44) r.push_back(60), r.push_back(100);
  for (unsigned w : words) r.push_back((uint8_t)(w >> 7 & 0x7F)), r.push_back((uint8_t)(w & 0x7F));
  if (tag) r.push_back(0), r.push_back(0);
  r.push_back(0xF7);
  return r;
}
static std::vector<unsigned> zeros(unsigned n) { return std::vector<unsigned>(n, 0); }

static void feed(lagdiag::RxDiag& d, uint64_t us, const std::vector<uint8_t>& m) { d.on_sysex(us, m.data(), (int)m.size()); }

static void rx_tests() {
  using lagdiag::RxDiag;
  // the report's length and layout are voices.cpp voice_report's: sizes of the messages built here
  CHECK(report(0x03, 0, zeros(16)).size() == 41 && report(0x03, 0, zeros(8), false).size() == 23);
  CHECK(report(0x04, 0, zeros(16)).size() == 43 && report(0x04, 0, zeros(8), false).size() == 25);

  RxDiag d;
  RxDiag::Second s;
  CHECK(!d.take(&s));   // nothing yet

  // voice 5 at 94 reports a second: counts, and the longest gap between two reports of the SAME voice
  std::vector<unsigned> w = zeros(16);
  uint64_t t = 1000000;
  feed(d, t, report(0x03, 5, w));
  feed(d, t + 10700, report(0x03, 5, w));
  feed(d, t + 12000, report(0x03, 2, w));   // another voice in between: not a gap of voice 5's
  feed(d, t + 21400, report(0x03, 5, w));
  feed(d, t + 33000, report(0x03, 5, w));   // 11600 after the last of voice 5
  CHECK(!d.take(&s));                        // the second is not over
  d.tick(t + 1000000);                       // a second after the first report
  CHECK(d.take(&s));
  CHECK(s.n[5] == 4 && s.gap_us[5] == 11600 && s.n[2] == 1 && s.gap_us[2] == 0);
  CHECK(s.sx_n == 5);
  CHECK(!d.take(&s));   // taken once

  // which words changed (a 16-bit mask over the second); the first report of a voice has nothing to differ from
  RxDiag m;
  t = 5000000;
  feed(m, t, report(0x03, 1, w));
  std::vector<unsigned> w2 = w;
  w2[3] = 700;
  feed(m, t + 10000, report(0x03, 1, w2));   // word 3 (both bytes) differs
  std::vector<unsigned> w3 = w2;
  w3[12] = 1;   // the low byte alone
  feed(m, t + 20000, report(0x03, 1, w3));
  feed(m, t + 30000, report(0x03, 1, w3));   // unchanged
  m.tick(t + 1000000);
  CHECK(m.take(&s));
  CHECK(s.mask[1] == ((1u << 3) | (1u << 12)));
  // a short report (8 words) compares 8 words; a 04 carries its note first and the same words; the MIDI machine's 43 / 44 too
  RxDiag q;
  t = 9000000;
  feed(q, t, report(0x04, 3, zeros(8), false));
  std::vector<unsigned> v8 = zeros(8);
  v8[7] = 5;
  feed(q, t + 5000, report(0x04, 3, v8, false));
  feed(q, t + 10000, report(0x44, 3, v8, false));   // (the MIDI machine's 04: same layout)
  feed(q, t + 15000, report(0x43, 3, v8, false));
  q.tick(t + 1000000);
  CHECK(q.take(&s));
  CHECK(s.n[3] == 4 && s.mask[3] == (1u << 7));
  // a 16-word report after an 8-word one: the words 8..15 are new, not "changed"
  RxDiag z;
  feed(z, t, report(0x03, 0, zeros(8), false));
  std::vector<unsigned> big = zeros(16);
  big[9] = 99;
  feed(z, t + 1000, report(0x03, 0, big));
  feed(z, t + 2000, report(0x03, 0, big));
  z.tick(t + 1000000);
  CHECK(z.take(&s));
  CHECK(s.mask[0] == 0);

  // SysEx inter-arrival (every SysEx from the Rytm, reports or not): longest, and the jitter, the mean change between intervals
  RxDiag a;
  t = 2000000;
  const std::vector<uint8_t> other = {0xF0, 0x7D, 0x52, 0x54, 0x49, 1, 2, 0xF7};   // a 49: not a report
  feed(a, t, other);
  feed(a, t + 10000, report(0x03, 0, w));
  feed(a, t + 20000, other);
  feed(a, t + 31000, report(0x03, 0, w));   // intervals 10000 10000 11000
  a.tick(t + 1000000);
  CHECK(a.take(&s));
  CHECK(s.sx_n == 4 && s.sx_max_us == 11000 && s.sx_jit_us == 500);   // (0 + 1000) / 2
  CHECK(s.n[0] == 2);   // only the two reports are a voice's

  // only while the stream is active: a second with no report publishes nothing (other SysEx alone, nothing at all)
  RxDiag n;
  feed(n, t, other);
  feed(n, t + 500, other);
  n.tick(t + 1000000);
  CHECK(!n.take(&s));
  n.tick(t + 5000000);
  CHECK(!n.take(&s));
  // things that are not reports: too short, a foreign manufacturer, a report-looking command with no body
  RxDiag f;
  const std::vector<uint8_t> foreign = {0xF0, 0x41, 0x52, 0x54, 0x03, 0x08, 0xF7};
  feed(f, t, foreign);
  const std::vector<uint8_t> stub = {0xF0, 0x7D, 0x52, 0x54, 0x03, 0x08, 0xF7};
  feed(f, t + 100, stub);
  f.tick(t + 1000000);
  CHECK(!f.take(&s));

  // a pause is not a gap: after more than a second (the stream stopped and started again) the first report only restarts it
  RxDiag p;
  feed(p, t, report(0x03, 4, w));
  feed(p, t + 3000000, report(0x03, 4, w));   // 3 s later
  feed(p, t + 3010000, report(0x03, 4, w));
  p.tick(t + 4100000);
  CHECK(p.take(&s));   // (both seconds: the second one is the one taken; the first rolled at the second report)
  CHECK(s.n[4] == 2 && s.gap_us[4] == 10000);

  // the second after a published one: its first gap reaches back over the roll (the stream never paused)
  RxDiag r;
  t = 1000;
  for (uint64_t k = 0; k < 150; ++k) feed(r, t + k * 10000, report(0x03, 6, w));   // 1.5 s of 100 a second
  CHECK(r.take(&s));                     // the first second rolled by itself, at the report that came after it
  CHECK(s.n[6] == 100 && s.gap_us[6] == 10000);
  r.tick(t + 2000000);
  CHECK(r.take(&s) && s.n[6] == 50 && s.gap_us[6] == 10000);

  // the line: voices that reported only, one compact line, capped
  RxDiag::Second x = {};
  x.n[5] = 94, x.gap_us[5] = 10700, x.mask[5] = 0x0003;
  x.n[0] = 3, x.gap_us[0] = 400000, x.mask[0] = 0x0ff0;
  x.sx_n = 121, x.sx_max_us = 11200, x.sx_jit_us = 410;
  char b[lagdiag::kLine + 8];
  unsigned len = lagdiag::fmt_rx(b, sizeof b, x);
  CHECK(len == strlen(b));
  CHECK(std::string(b) == "rxrep: v0 n3 gap400000us m0ff0 | v5 n94 gap10700us m0003 | sx n121 max11200us jit410us");
  for (unsigned v = 0; v < 8; ++v) x.n[v] = 65535, x.gap_us[v] = 4000000000u, x.mask[v] = 0xffff;
  x.sx_n = 65535, x.sx_max_us = 4000000000u, x.sx_jit_us = 4000000000u;
  len = lagdiag::fmt_rx(b, lagdiag::kLine, x);   // every voice at its widest: capped, with the voices left out counted
  CHECK(len == strlen(b) && len < lagdiag::kLine);
  CHECK(std::string(b).find("sx n65535 max4000000000us jit4000000000us") != std::string::npos);
  CHECK(std::string(b).find("more") != std::string::npos);
  CHECK(std::string(b).find("v0 ") != std::string::npos);   // (the first voices kept)

  // the two cores: a writer rolling seconds, a reader taking them, never a torn one (every field of a second is one number)
  RxDiag live;
  std::atomic<bool> stop(false);
  std::thread core3([&] {
    uint64_t us = 0;
    while (!stop.load()) {
      for (int k = 0; k < 3; ++k) {   // three reports of one voice, then a second passes
        std::vector<unsigned> ww = zeros(16);
        ww[0] = (unsigned)(us / 1000000 % 100);
        feed(live, us, report(0x03, 7, ww));
        us += 400000;
      }
    }
  });
  unsigned seen = 0, torn = 0;
  auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(300)) {
    RxDiag::Second y;
    if (live.take(&y)) {
      ++seen;
      // 1.2 s of stream a turn: the seconds hold 2 or 3 reports of voice 7 and nothing of the others
      if (y.n[7] < 2 || y.n[7] > 3 || y.sx_n != y.n[7] || y.n[0] != 0 || y.gap_us[7] != 400000) ++torn;
    }
  }
  stop = true;
  core3.join();
  CHECK(seen > 100 && torn == 0);

  // the cost of a report on the receive path (the host's figure is printed; the Pi's is estimated in the report)
  RxDiag cost;
  const std::vector<uint8_t> rep = report(0x03, 5, zeros(16));
  auto c0 = std::chrono::steady_clock::now();
  const unsigned N = 200000;
  for (unsigned k = 0; k < N; ++k) cost.on_sysex(1000000ull + k * 10000ull, rep.data(), (int)rep.size());
  double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - c0).count() / N;
  printf("test_lagdiag: rx report %.0f ns (this build of the host)\n", ns);
}

int main() {
  marker_tests();
  rx_tests();
  printf("test_lagdiag: PASS\n");
  return 0;
}
