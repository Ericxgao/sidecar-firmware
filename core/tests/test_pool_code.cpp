// core/tests/test_pool_code.cpp - the address of a SAMPLES row for a plugin's pool_info / pool_read (API 2).
// Two address spaces meet there: the lists and the Rytm use the code bank * 128 + row; the host's pool lines are
// bank * 127 + row (pool_slot). Facts checked: what a plugin's SAMPLES knob holds (the row, the bank in the fraction),
// what the old slot meaning reached (bank A only, anything computed from the list code the wrong row past bank A), and
// that um::pool_line takes both the old bank-A slot (1..127) and the list code and lands on the row's own sample.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "check.h"
#include "lib/library.h"
#include "lib/pools.h"
#include "lib/srclib.h"
#include "machines.h"
#include "msamp/msamp.h"
#include "nmach.h"
#include "pool/pool.h"
#include "pool_util.h"
#include "user/um.h"
#include "voices.h"
#include "wg_util.h"

using namespace core;

static std::string g_card;
static uint64_t g_us = 50000000;
static void idle() {
  for (int i = 0; i < 20000; ++i) {
    host_card_core(0);
    bool c = lib::step();
    host_card_core(3);
    bool a = nm::step(), b = msamp::step();
    if (!a && !b && !c) break;
  }
}

static std::string row_name(int bank, int row) { return std::string("s") + (char)('a' + bank) + std::to_string(row) + ".wav"; }

int main() {
  char tmpl[] = "/tmp/poolcode-test-XXXXXX";
  CHECK(mkdtemp(tmpl));
  g_card = tmpl;
  CHECK(system(("mkdir -p '" + g_card + "/Samples/t' '" + g_card + "/Plugins'").c_str()) == 0);
  const int rows[] = {20, 21, 50, 127};   // (bank H's rows 1..10 are the live inputs, 11.. built-ins: not files)
  for (int b = 0; b < 8; ++b)
    for (int r : rows)
      wg::write_file(g_card + "/Samples/t/" + row_name(b, r), wg::wav_bytes(480 + b * 16 + r, 1, 16, 48000, 1, "", [](int i, int) { return 0.1 * std::sin(i * 0.1); }));
  host_set_card(g_card.c_str());
  hooks.to_rytm = [](const uint8_t*, int) {};
  host_fake_time(true, g_us);
  setenv("CORE_MSAMP_RAM_MB", "64", 1);
  voices_init();
  card_ok = true;
  lib::begin(true);
  nm::begin(true);
  msamp::begin(true);
  pu::steps();
  idle();
  for (int b = 0; b < 8; ++b)
    for (int r : rows) {
      char row[8];
      snprintf(row, sizeof row, "%c%03d", 'A' + b, r);
      CHECK(pu::put(pools::kSamples, row, ("/Samples/t/" + row_name(b, r)).c_str()) == pools::kOk);
    }
  pool_sync();

  // 1. what a plugin's SAMPLES knob holds: the report's integer is the row, its fraction (lo = bank << 4) the bank
  const uint8_t raw[] = {5, 2 << 4};   // row 5, bank C
  float p[1];
  decode_params(raw, 1, p);
  CHECK((int)p[0] == 5);                                      // `(int)p[4]`: the row, whatever the bank
  CHECK(srclib::code_of(p[0]) == 2 * 128 + 5);                // the list code the lists and the Rytm use
  CHECK(pool_slot(p[0]) == 2 * kPool + 5);                    // the host's own pool line

  // 2. the old meaning: a slot was a pool line (bank * 127 + row): bank A only for 1..127, and the list code of any other
  //    bank (bank * 128 + row) read as a line is another row's sample
  for (int b = 1; b < 8; ++b)
    for (int r : rows) {
      int code = b * 128 + r, line = b * kPool + r;
      CHECK(code != line);
      if (code <= npool) CHECK(strcmp(pool[code - 1].path, pool[line - 1].path) != 0);   // (the wrong row, when there is one)
    }

  // 3. the fix: pool_line takes the list code and the old bank-A slot, and gives the row's own pool line
  for (int b = 0; b < 8; ++b)
    for (int r : rows) {
      int code = b * 128 + r, line = um::pool_line(code);
      CHECK(line == b * kPool + r);
      CHECK(line >= 1 && line <= npool);
      CHECK(!strcmp(pool[line - 1].path, ("t/" + row_name(b, r)).c_str()));
      if (b == 0) CHECK(um::pool_line(r) == line);   // the old slot of bank A: the same line
    }
  CHECK(um::pool_line(7 * 128 + 1) == 7 * kPool + 1 && pool[7 * kPool].folder < 0);   // H001: an input, no file: EMPTY to a plugin
  CHECK(um::pool_line(0) == 0 && um::pool_line(-1) == 0);
  CHECK(um::pool_line(128) == 0 && um::pool_line(7 * 128) == 0);   // row 0 of a bank is OFF
  CHECK(um::pool_line(8 * 128) == 0 && um::pool_line(100000) == 0);   // no bank I
  CHECK(um::pool_line(1) == 1 && um::pool_line(127) == 127);

  system(("rm -rf '" + g_card + "'").c_str());
  printf("test_pool_code: PASS\n");
  return 0;
}
