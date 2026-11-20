// pi/engine/tests/test_lagdiag.cpp - the lag diagnostics (lagdiag.h): the "Lag now" marker and its snapshot (part 1),
// the Rytm report-stream timing (part 2) and the Pi-to-Rytm accounting (part 3).
#include <cstring>
#include <string>

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

int main() {
  marker_tests();
  printf("test_lagdiag: PASS\n");
  return 0;
}
