// Bounded, read-only native UI metadata. Audio events keep their own protocol.
// A request names the complete engine ID. Replies echo session + request serial;
// a descriptor transaction must use one revision throughout before publication.
#pragma once
#include "dn2_native.h"
#include <string.h>

namespace dn2_native {
constexpr unsigned kUiAskBytes = 50, kUiReplyMax = 128, kUiHeader = 22;
constexpr unsigned kUiContextAskBytes = 52;
enum UiOp : uint8_t { UiMeta = 0, UiNames = 1, UiCatalog = 2, UiLoad = 3 };
struct UiAsk {
    uint32_t token = 0, serial = 0;
    uint8_t op = 0, arg = 0, first = 0;
    char id[32]{};
    uint8_t version = 2, track = 0, selector = 0;
};
struct UiReply {
    uint32_t epoch = 0;
    uint16_t bytes = 0;
    uint8_t packet[kUiReplyMax]{};
};
inline uint32_t ui_get7(const uint8_t* p, unsigned n) {
    uint32_t v = 0;
    while (n--) v = (v << 7) | *p++;
    return v;
}
inline void ui_put7(uint8_t* p, uint32_t v, unsigned n) {
    while (n) { p[--n] = uint8_t(v & 127); v >>= 7; }
}
inline bool ui_ask_ok(const UiAsk& a) {
    if(a.version!=2 && a.version!=3) return false;
    if(a.version==3 && (a.track>=8 || a.selector>127 || a.op>UiNames)) return false;
    if (!a.token || a.token > 0x0fffffff || !a.serial || a.serial > 0x0fffffff) return false;
    if (a.op == UiCatalog) {
        if (a.arg >= 32 || a.first) return false;
        for (char c : a.id) if (c) return false;
        return true;
    }
    if (!engine_id_ok(a.id)) return false;
    return (a.op == UiMeta && a.arg <= 4 && !a.first) ||
           (a.op == UiNames && a.arg < 16 && a.first <= 32 && !(a.first & 7)) ||
           (a.op == UiLoad && a.arg < 16 && !a.first);
}
inline bool ui_decode_ask(const uint8_t* p, size_t n, UiAsk& out) {
    if (!p || (n!=kUiAskBytes && n!=kUiContextAskBytes) || p[0] != 0xf0 || p[1] != 0x7d || p[2] != 0x44 ||
        p[3] != 0x32 || (p[4]!=2 && p[4]!=3) || p[5] != 3 || p[n-1] != 0xf7 ||
        n!=(p[4]==3?kUiContextAskBytes:kUiAskBytes)) return false;
    for (unsigned i = 6; i < n-1; ++i) if (p[i] & 128) return false;
    UiAsk a;
    a.token = ui_get7(p + 6, 4); a.serial = ui_get7(p + 10, 4);
    a.op = p[14]; a.arg = p[15]; a.first = p[16];
    a.version=p[4];
    if(a.version==3) {a.track=p[17];a.selector=p[18];}
    memcpy(a.id, p + (a.version==3?19:17), 32);
    if (!ui_ask_ok(a)) return false;
    out = a;
    return true;
}
inline bool ui_encode_ask(const UiAsk& a, uint8_t* p, size_t capacity) {
    unsigned bytes=a.version==3?kUiContextAskBytes:kUiAskBytes;
    if (!p || capacity < bytes || !ui_ask_ok(a)) return false;
    const uint8_t head[] = {0xf0, 0x7d, 0x44, 0x32, a.version, 3};
    memcpy(p, head, sizeof head);
    ui_put7(p + 6, a.token, 4); ui_put7(p + 10, a.serial, 4);
    p[14] = a.op; p[15] = a.arg; p[16] = a.first;
    if(a.version==3) {p[17]=a.track;p[18]=a.selector;}
    memcpy(p + (a.version==3?19:17), a.id, 32); p[bytes-1] = 0xf7;
    return true;
}
inline void ui_reply_head(const UiAsk& a, uint32_t revision, UiReply& r) {
    const uint8_t head[] = {0xf0, 0x7d, 0x44, 0x32, a.version, 4};
    memcpy(r.packet, head, sizeof head);
    ui_put7(r.packet + 6, a.token, 4); ui_put7(r.packet + 10, a.serial, 4);
    r.packet[14] = a.op; r.packet[15] = a.arg; r.packet[16] = a.first;
    ui_put7(r.packet + 17, revision, 5);
}
// Transport validation only. Descriptor-specific validation belongs to the client.
inline bool ui_reply_ok(const UiReply& r, uint32_t epoch, uint32_t token) {
    const uint8_t* p = r.packet;
    if (!token || token > 0x0fffffff || r.epoch != epoch || r.bytes < kUiHeader + 1 ||
        r.bytes > kUiReplyMax || p[0] != 0xf0 || p[1] != 0x7d || p[2] != 0x44 ||
        p[3] != 0x32 || (p[4]!=2 && p[4]!=3) || (p[4]==3 && p[14]>UiNames) || p[5] != 4 || p[r.bytes - 1] != 0xf7 ||
        ui_get7(p + 6, 4) != token || !ui_get7(p + 10, 4) || p[17] > 15) return false;
    for (unsigned i = 6; i < r.bytes - 1u; ++i) if (p[i] & 128) return false;
    return (p[14] == UiMeta && p[15] <= 4 && !p[16]) ||
           (p[14] == UiNames && p[15] < 16 && p[16] <= 32 && !(p[16] & 7)) ||
           (p[14] == UiCatalog && p[15] < 32 && !p[16]) ||
           (p[14] == UiLoad && p[15] < 16 && !p[16] && r.bytes == 24 && p[22] <= 6);
}
} // namespace dn2_native
