// Real device.cpp, notes.cpp and MIDI parser; filesystem/DSP/output are stubs.
#include "platform.h"
#include "device/dn2.h"
#include "device/digitakt.h"
#include "device/strip.h"
#include "notes.h"
#include "midi_in.h"
#include "dn2_midi.h"
#include "usb_box.h"
#include "check.h"
#include <vector>
namespace core {
namespace out { void set_trim(int, int) {} }
namespace device { namespace strip {
void report(const Controls&, int, const uint8_t*) {}
void trig(int) {} void off(int) {} void reset() {} void tick(const Controls&, uint32_t) {}
}}
}
using namespace core;
static unsigned outgoing;
static std::vector<std::vector<uint8_t>> reports;
static void to_box(const uint8_t*, int) { ++outgoing; }
static bool translate(std::vector<uint8_t> data) {
  const uint8_t* p = data.data();
  int n = int(data.size());
  uint8_t scratch[device::kMaxMessage];
  bool ok = device::translate(device::g_link, p, n, scratch);
  if (ok) reports.emplace_back(p, p + n);
  return ok;
}
static void report(const uint8_t* p, int n) { CHECK(translate({p, p + n})); }
static NoteAdapter notes(report);
static void channel(uint8_t s, uint8_t a, uint8_t b) { if (dn2_channel(s, a, b, 10)) notes.Channel(s, a, b); }
static void sysex(const uint8_t*, int) {} // stock DN2 bring-up ignores device SysEx
static void realtime(uint8_t s, uint16_t) { if (s == 0xfc) notes.Channel(0xb0, 123, 0); }
int main() {
  hooks.to_rytm = to_box;
  CHECK(device::by_id(device::kIdDn2) == nullptr);
  CHECK(device::add(&device::kDn2));
  CHECK(device::by_usb(device::kDn2Vid, device::kDn2Pid) == nullptr);
  test_saved = "2\n"; // previous Digitakt choice must remain intact
  CHECK(!device::boot_as(999) && device::boot_as(device::kIdDn2));
  device::boot();
  CHECK(device::active().id == device::kIdDn2 && test_opens == 0);
  CHECK(test_directory == "/.dn2" && test_saved == "2\n");
  CHECK(!device::boot_as(device::kIdDigitakt));
  CHECK(device::by_usb(device::kDn2Vid, device::kDn2Pid) == &device::kDn2);
  CHECK(device::by_usb(0x1935, 0x0b48) == nullptr);
  char path[80];
  CHECK(std::string(device::state_path("/.rytm/projects/p", path, sizeof path)) == "/.dn2/projects/p");
  CHECK(std::string(device::state_path("/.rytm/wifi.txt", path, sizeof path)) == "/.rytm/wifi.txt");
  for (int cmd = 0; cmd < 128; ++cmd) {
    uint8_t m[] = {0xf0, 0x7d, 0x52, 0x54, uint8_t(cmd), 0xf7};
    hooks.to_rytm(m, sizeof m);
  }
  CHECK(outgoing == 0);
  CHECK(!translate({0xf0, 0x7d, 0x52, 0x54, 0x35, 0, 0xf7}));
  CHECK(!translate({0xf0, 0x7d, 0x52, 0x54, 0x4f, 1, 0, 2, 1, 0xf7}));
  device::rytm_on_usb(true);
  CHECK(!device::step() && !device::leaving() && test_posts == 0 && outgoing == 0);
  device::box_note(0, 60, 100);
  CHECK(test_forwarded == 0);
  for (uint8_t part = 0; part < 8; ++part) {
    CHECK(translate({0xf0, 0x7d, 0x52, 0x54, 0x11, part, 0xf7}));
    CHECK(reports.back()[5] == part);
  }
  reports.clear();

  for (unsigned selected = 1; selected <= 16; ++selected)
    for (unsigned input = 0; input < 16; ++input) {
      uint8_t s = uint8_t(0x90 | input);
      CHECK(dn2_channel(s, 60, 100, selected) == (input + 1 == selected));
      if (input + 1 == selected) CHECK(s == 0x90);
    }
  uint8_t s = 0x99;
  CHECK(!dn2_channel(s, 60, 100, 0) && !dn2_channel(s, 60, 100, 17));
  s = 0xb9;
  CHECK(!dn2_channel(s, 7, 100, 10)); // native track volume must not change a synth parameter
  s = 0xc9;
  CHECK(!dn2_channel(s, 16, 0, 10));

  MidiIn parser(sysex, channel, realtime);
  auto feed = [&](std::initializer_list<uint8_t> bytes) { for (auto b : bytes) parser.Feed(b); };
  feed({0x98, 50, 100}); CHECK(reports.empty());
  feed({0x99, 60, 100, 0xf8, 62, 110}); // running status, clock interleaved
  CHECK(reports.size() == 2 && reports[0][4] == 4 && reports[0][5] == 0 && reports[0][6] == 60);
  CHECK(reports[1][6] == 62 && reports[1][7] == 110);
  feed({0x89, 60, 0}); CHECK(reports.size() == 2); // old off cannot stop the newer note
  feed({0x99, 62, 0}); CHECK(reports.size() == 3 && reports.back()[4] == 0x11);
  feed({0xb9, 16, 20}); CHECK(reports.back()[4] == 3 && reports.back()[6] == 20);
  feed({0x99, 65, 100, 0xfc}); CHECK(reports.back()[4] == 0x11); // STOP releases
  feed({0xf0, 0x7d, 0x52, 0x54, 0x35, 0xf7, 0x99, 60});
  const auto count = reports.size();
  parser.ResetStream(); feed({100, 61, 100}); CHECK(reports.size() == count); // unplug clears partial/running status
  notes.Channel(0xb0, 120, 0); CHECK(test_silenced == 1);
  feed({0x99, 64, 90}); CHECK(reports.size() == count + 1);

  { // A stock MIDI track can select a loaded user machine without hardcoded engine defaults.
    auto defaults = [](int slot, uint8_t p[16]) -> bool {
      if (slot == 3) return false; // unavailable slot must not replace the current machine
      for (int k = 0; k < 16; ++k) p[k] = uint8_t(10 + slot + k);
      return true;
    };
    NoteAdapter user([](const uint8_t* d, int n) { CHECK(translate({d, d+n})); }, defaults);
    NoteAdapter unavailable(nullptr);
    CHECK(!unavailable.SetMachine(0, "u1"));
    CHECK(!user.SetMachine(0, "u0") && !user.SetMachine(0, "u6") && !user.SetMachine(0, "u1x"));
    CHECK(!user.SetMachine(-1, "u1") && !user.SetMachine(8, "u1") && !user.SetMachine(0, nullptr));
    for (int slot : {0, 1, 2, 4}) {
      char name[] = {'u', char('1' + slot), 0};
      CHECK(user.SetMachine(0, name));
      CHECK(user.Machine(0) == 0x50 + 8*slot && !strcmp(NoteAdapter::Name(user.Machine(0)), name));
      user.Channel(0x90, 60, 100);
      const auto& m = reports.back();
      CHECK(m.size() == 41 && m[4] == 4 && m[5] == 0x50 + 8*slot && m[6] == 60 && m[7] == 100);
      for (int k = 0; k < 16; ++k) CHECK(m[8+2*k] == 10+slot+k && m[9+2*k] == 0);
      user.Channel(0xb0, 31, 127);
      CHECK(reports.back()[4] == 3 && reports.back()[36] == 127);
      user.Channel(0x90, 62, 110); // held parameter survives retrigger
      CHECK(reports.back()[38] == 127);
      auto n = reports.size();
      user.Channel(0x80, 60, 0); CHECK(reports.size() == n);
      user.Channel(0xb0, 123, 0); CHECK(reports.back()[4] == 0x11);
      CHECK(!user.SetMachine(0, "u4") && user.Machine(0) == 0x50 + 8*slot);
      user.Channel(0x90, 67, 100);
      n = reports.size(); auto silenced = test_silenced;
      user.SuspendVoice(0);
      CHECK(reports.size() == n && test_silenced == silenced + 1);
      user.RestoreVoice(0);
      CHECK(reports.size() == n + 1 && reports.back()[4] == 3 && reports.back()[5] == 0x50 + 8*slot);
      CHECK(reports.back()[36] == 127); // retained CC value, not manifest defaults
      user.Channel(0x80, 67, 0); CHECK(reports.size() == n + 1); // old held note forgotten
      user.SuspendVoice(-1); user.RestoreVoice(8); CHECK(reports.size() == n + 1);
    }
  }

  CHECK(usb_box("Elektron", "Elektron Digitone II", true) == UsbBoxHost);
  CHECK(usb_box("Elektron", "Elektron Digitone II") == UsbBoxOther);
  CHECK(usb_box("Elektron", "Elektron Digitone", true) == UsbBoxOther);
  CHECK(usb_box("Elektron", "Elektron Digitone III", true) == UsbBoxOther);
  CHECK(usb_box("Elektron", "Elektron Analog Rytm MKII", true) == UsbBoxOther);
  CHECK(usb_box(nullptr, nullptr, true) == UsbBoxOther);
  CHECK(usb_box("Arturia", "KeyStep", true) == UsbBoxKeyboard);

  device::reset(); // removing dn2=1 restores the previous remembered box
  CHECK(device::by_usb(device::kDn2Vid, device::kDn2Pid) == nullptr);
  device::boot(); CHECK(device::active().id == device::kIdDigitakt && test_saved == "2\n");
  CHECK(!device::boot_as(device::kIdDn2));
  device::reset(); test_saved.clear(); device::boot();
  CHECK(device::active().id == device::kIdRytm);
  std::puts("PASS: explicit DN2 selection, isolated state, no replies, channel routing, notes/CC/stop/parser reset");
}
