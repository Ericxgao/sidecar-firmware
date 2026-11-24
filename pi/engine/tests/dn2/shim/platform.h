// In-memory platform for tests of the real device selection and note adapter.
#pragma once
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <string>
#define CORE_COLD
#define CORE_STR(s) (s)
#define sniprintf snprintf
namespace core {
constexpr int kVoices = 8, kRead = 0;
struct Hooks { void (*to_rytm)(const uint8_t*, int) = nullptr; void (*device_restart)() = nullptr; };
inline Hooks hooks;
inline unsigned test_opens = 0, test_posts = 0, test_silenced = 0, test_forwarded = 0;
inline std::string test_saved, test_directory;
struct File {
  bool open(const char*, int) { ++test_opens; return !test_saved.empty(); }
  int read(char* b, unsigned n) { if (n > test_saved.size()) n = unsigned(test_saved.size()); memcpy(b, test_saved.data(), n); return int(n); }
  void close() {}
};
struct Fs { bool mkdir(const char* p) { test_directory = p; return true; } };
inline uint32_t now_ms() { return 100; }
inline void log(const char*, ...) {}
}
