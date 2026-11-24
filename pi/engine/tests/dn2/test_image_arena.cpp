#include "user/page_arena.h"
#include "rmp_slots.h"
#include "rmp_resolve.h"
#include "rmp_proto.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  constexpr unsigned size = 4u << 20;
  static uint8_t memory[size];
  um::PageArena<size> arena;
  assert(!arena.alloc(memory, 0) && !arena.alloc(memory, 0xffffffff));
  auto a = arena.alloc(memory, 4096), b = arena.alloc(memory, 8192);
  assert(a == memory && b == memory + 4096);
  assert(!arena.release(memory, b + 4096, 4096)); // no interior/double/wrong-size frees
  assert(!arena.release(memory, b, 4096));
  assert(arena.release(memory, a, 4096));
  assert(!arena.release(memory, a, 4096));
  assert(arena.alloc(memory, 4096) == a);
  assert(arena.release(memory, a, 4096) && arena.release(memory, b, 8192));
  assert(arena.alloc(memory, size) == memory);
  assert(!arena.alloc(memory, 1));
  arena.reset();
  struct Block { uint8_t* p{}; unsigned n{}; uint8_t value{}; } live[12];
  unsigned rng = 12345;
  for (unsigned turn = 0; turn < 20000; ++turn) {
    rng = rng * 1664525 + 1013904223;
    auto& x = live[(rng >> 16) % 12];
    if (x.p) assert(arena.release(memory, x.p, x.n));
    x.n = (1 + (rng % 40)) * 4096; x.value = uint8_t(turn);
    x.p = arena.alloc(memory, x.n); assert(x.p);
    std::memset(x.p, x.value, x.n);
    for (const auto& y : live) if (y.p)
      for (unsigned j = 0; j < y.n; j += 4096) assert(y.p[j] == y.value && y.p[j + 4095] == y.value);
  }
  for (const auto& x : live) assert(arena.release(memory, x.p, x.n));
  assert(arena.alloc(memory, size) == memory); // every byte reclaimed/coalesced

  rmp_slot_map map{}, parsed{};
  rmp_slot_notes notes{}, read_notes{};
  for (int i = 0; i < RMP_SLOTS; ++i) {
    char id[32];
    std::snprintf(id, 32, "engine%02d.abcdefghijklmnopqrstuv", i);
    map.slot[i].version = 0xffffff;
    rmp_resolve_set(&map, &notes, i, id, 0xffffff, RMP_SRC_SAVED);
    notes.slot[i].flags = 255; notes.slot[i].fp = 16383;
    std::strcpy(notes.slot[i].name, "abcdefghij");
  }
  char txt[512], inf[1024];
  auto n = rmp_slots_format(&map, txt, sizeof txt);
  assert(n && rmp_slots_parse(txt, n, &parsed) && !std::memcmp(&map, &parsed, sizeof map));
  n = rmp_notes_format(&notes, inf, sizeof inf);
  assert(n && rmp_notes_parse(inf, n, &read_notes) && !std::memcmp(&notes, &read_notes, sizeof notes));
  const char* old = "U1 mega.perkons 0.1.2\nU5 demo.bell 0.1.2\n";
  assert(rmp_slots_parse(old, std::strlen(old), &parsed));
  assert(!rmp_slots_parse("U9 invalid.engine 0.1.0\n", 22, &parsed));
  assert(rmp_slot_of_flag(0x70) == 4 && rmp_slot_of_flag(0x78) == -1);
  uint8_t wire[128], generations[5] = {1,2,3,4,5};
  assert(rmp_build_slots(wire, generations, 1, 0, 0) == 13); // unchanged five-slot packet
  std::printf("PASS: 20,000 swaps preserve live pages and reclaim all memory; %d-slot map/notes round trips; legacy wire unchanged\n", RMP_SLOTS);
}
