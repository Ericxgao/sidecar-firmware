#pragma once
#include <stdint.h>
#include "output.h"
#include "records.h"
#include "spsc.h"
#include "dn2_transport.h"
#include "dn2_ui_wire.h"
#include <deque>
#include <vector>
struct TToHostStub {   // the engine's records for the Rytm, as the test queues them
  std::deque<std::vector<uint8_t>> q;
  std::vector<uint8_t> cur;
  const uint8_t* Next(int* len) {
    if (q.empty()) return nullptr;
    cur = q.front(), q.pop_front();
    *len = (int)cur.size();
    return cur.data();
  }
  unsigned Waiting() const { unsigned n = 0; for (auto& r : q) n += (unsigned)r.size() + 2; return n; }
  unsigned Drain() { unsigned n = 0; for (auto& r : q) n += (unsigned)r.size(); q.clear(); return n; }
};
inline Output g_Output;
inline TToHostStub g_ToHost;
inline Spsc<uint32_t, 512> g_KeysToRytm;
inline unsigned g_nToHostFull;
inline volatile bool g_bRytmResync;
inline volatile bool g_bRytmOff;
inline Spsc<dn2_native::LinkRequest, 4> g_Dn2LinkOut;
inline Spsc<dn2_native::UiReply, 2> g_Dn2UiOut;
inline uint32_t g_nDn2UiToken;
inline uint32_t g_nDn2Transport;
