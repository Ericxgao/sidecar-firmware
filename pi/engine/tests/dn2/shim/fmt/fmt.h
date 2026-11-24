#pragma once
#include "platform.h"
namespace core { namespace fmt {
enum PostResult { kPostPending, kPostOk };
inline void recover(const char*) {}
inline bool post_queue(const char*, const char*, uint32_t, uint32_t*) { ++test_posts; return true; }
inline PostResult post_result(uint32_t) { return kPostOk; }
}}
