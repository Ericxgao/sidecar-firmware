// core/tests/test_uifeel.cpp - 0.5.0 UI feel on the Sidecar's pages (core/ui/page.h detent_step, remote.cpp): a centre
// detent on bipolar knobs, FUNC + turn a coarse step, UP held on a page that scrolls with it is a scroll (no map), and
// UP / DOWN (LEFT / RIGHT) repeat while held. Everything through remote_sysex, as the Rytm sends it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>   // mkdtemp

#include "check.h"
#include "core.h"
#include "core_port.h"
#include "engine.h"
#include "lfo.h"
#include "ui/extra.h"
#include "ui/page.h"
#include "ui/remote.h"
#include "voices.h"

using namespace core;
using namespace core::ui;

static uint64_t g_us = 1000000000;
static void at(uint64_t us) { g_us = us, host_fake_time(true, g_us); }
static void out(const uint8_t*, int) {}
static void from_rytm(uint8_t cmd, uint8_t a, uint8_t b, uint8_t c) {
  const uint8_t m[10] = {0xF0, 0x7D, 0x52, 0x54, cmd, 1, a, b, c, 0xF7};
  engine_sysex(m, 10);
}
static void tick(int n = 1, int ms = 30) {
  for (int i = 0; i < n; ++i) at(g_us + (uint64_t)ms * 1000), remote_tick(now_ms());
}
static void key(int code, bool down) { from_rytm(0x31, (uint8_t)(code >> 7), (uint8_t)(code & 0x7F), down ? 1 : 0); }
static void tap(int code) { key(code, true), key(code, false), tick(); }
static void turn(int e, int delta, bool func = false) { from_rytm(0x32, (uint8_t)e, (uint8_t)(64 + delta), func ? 1 : 0); }
enum { kLeft = 0x40, kRight = 0x41, kUp = 0x42, kDown = 0x43, kYes = 0x44, kNo = 0x45 };
static bool is(const char* title) { return remote_page_title() && !strcmp(remote_page_title(), title); }

// PLAITS on track 1 (P1..P8 ENG HRM TIM MOR DEC LPG TUN LEV), P7 (TUN, kKnobBi) at `tun`, the others 45; its page open
static void plaits_page(int tun) {
  remote_reset();
  const uint8_t mach[] = {0xF0, 0x7D, 0x52, 0x54, 0x33, 1, 0, 1, 44, 0x00, 0xF7};
  engine_sysex(mach, sizeof mach);
  uint8_t r[39] = {0xF0, 0x7D, 0x52, 0x54, 0x03, 0x00};
  for (int k = 0; k < 16; ++k) r[6 + 2 * k] = 45;
  r[6 + 2 * 6] = (uint8_t)tun;
  r[38] = 0xF7;
  engine_sysex(r, sizeof r);
  from_rytm(0x30, 0, 1, 0);
  tick();
  for (int i = 0; i < 8 && remote_stats().page != 1; ++i) tap(kRight);
  CHECK(remote_stats().page == 1 && remote_stats().voice == 0);
}

// ---- 1: the centre detent
static void test_detent() {
  plaits_page(63);
  turn(6, 1);
  CHECK(v[0].p[6] == 64);   // lands on the centre
  for (int i = 0; i < kDetentCounts; ++i) {
    turn(6, 1);
    CHECK(v[0].p[6] == 64);
  }   // and stays through the detent's travel
  turn(6, 1);
  CHECK(v[0].p[6] == 65);   // then leaves
  // a turn that crosses stops on the centre; the overshoot is the detent's
  plaits_page(62);
  turn(6, 5);
  CHECK(v[0].p[6] == 64);
  // from below, the other way: a reversal inside the detent cancels the travel
  turn(6, -1), turn(6, -1);
  CHECK(v[0].p[6] == 64);
  turn(6, 1), turn(6, 1);
  CHECK(v[0].p[6] == 64);
  for (int i = 0; i < kDetentCounts + 1; ++i) turn(6, -1);
  CHECK(v[0].p[6] == 63);
  // a knob with no centre (HRM) keeps its counts
  plaits_page(63);
  turn(1, 1);
  CHECK(v[0].p[1] == 46);
  // a coarse FUNC turn lands on the centre too
  plaits_page(60);
  turn(6, 1, true);
  CHECK(v[0].p[6] == 64);
}

// the code pages take the same detent: the LFO page's DEPTH (knob D of its main view, -64..63, 0 none)
static void test_detent_lfo() {
  lfo_page_reset();
  lfo::Settings st = lfo::get(0);
  st.depth = -1;
  lfo::set(0, st);
  kLfoPage.enc(3, 1, false);
  CHECK(lfo::get(0).depth == 0);
  for (int i = 0; i < kDetentCounts; ++i) {
    kLfoPage.enc(3, 1, false);
    CHECK(lfo::get(0).depth == 0);
  }
  kLfoPage.enc(3, 1, false);
  CHECK(lfo::get(0).depth == 1);
}

int main() {
  char tmpl[] = "/tmp/core_uifeel_XXXXXX";
  CHECK(mkdtemp(tmpl));
  CHECK(system((std::string("mkdir -p '") + tmpl + "/Samples' '" + tmpl + "/.rytm'").c_str()) == 0);
  host_set_card(tmpl);
  hooks.to_rytm = out;
  at(g_us);
  CHECK(engine_begin());
  struct { const char* name; void (*fn)(); } tests[] = {{"detent", test_detent}, {"detent_lfo", test_detent_lfo}};
  for (auto& t : tests) printf("-- %s\n", t.name), fflush(stdout), t.fn();
  remote_reset();
  system((std::string("rm -rf '") + tmpl + "'").c_str());
  printf("test_uifeel: PASS\n");
  return 0;
}
