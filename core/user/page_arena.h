// Fixed-capacity, reusable 4 KiB blocks. Control-core only; no heap or locks.
#pragma once
#include <stdint.h>

namespace um {
template <uint32_t Bytes> class PageArena {
  static constexpr uint32_t kPage = 4096, kPages = Bytes / kPage;
  static_assert(Bytes && Bytes % kPage == 0 && kPages < 65535, "page arena size");
  // A head records its extent; continuation pages cannot be freed individually.
  uint16_t runs[kPages] = {};
public:
  void reset() { for (auto& r : runs) r = 0; }
  uint8_t* alloc(uint8_t* base, uint32_t bytes) {
    if (!base || !bytes || bytes > Bytes) return nullptr;
    uint32_t n = (bytes + kPage - 1) / kPage, start = 0, count = 0;
    for (uint32_t i = 0; i < kPages; ++i) {
      if (runs[i]) { count = 0; continue; }
      if (!count) start = i;
      if (++count != n) continue;
      runs[start] = (uint16_t)n;
      for (uint32_t j = start + 1; j <= i; ++j) runs[j] = 65535;
      return base + start * kPage;
    }
    return nullptr;
  }
  bool release(uint8_t* base, uint8_t* p, uint32_t bytes) {
    uintptr_t b = (uintptr_t)base, a = (uintptr_t)p;
    if (!base || !p || !bytes || bytes > Bytes || a < b || a - b >= Bytes || (a - b) % kPage) return false;
    uint32_t start = (uint32_t)(a - b) / kPage, n = (bytes + kPage - 1) / kPage;
    if (runs[start] != n || n > kPages - start) return false;
    for (uint32_t i = start; i < start + n; ++i) runs[i] = 0;
    return true;
  }
};
} // namespace um
