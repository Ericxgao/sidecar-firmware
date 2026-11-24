// One small response per native UI request. Metadata reads cached manifests only.
// The explicit Load operation may queue an assignment; no synchronous file reads.
#pragma once
#include "dn2_ui_wire.h"
#include "user/native_catalog.h"
#include "rmp_proto.h"

namespace dn2_native {
template<class Provider>
bool ui_answer(Provider& provider, const uint8_t* packet, size_t bytes,
               uint32_t token, uint32_t epoch, UiReply& out) {
    UiAsk a;
    if (!ui_decode_ask(packet, bytes, a) || a.token != token) return false;
    provider.Context(a.version==3 ? int(a.track) : -1,a.selector);
    UiReply r;
    r.epoch = epoch;
    unsigned n = kUiHeader;
    if (a.op == UiLoad) {
        const unsigned state = provider.Load(a.arg, a.id);
        if (state > RMP_PST_MUTED) return false;
        ui_reply_head(a, 0, r);
        r.packet[n++] = uint8_t(state);
    } else if (a.op == UiCatalog) {
        um::NativeCatalogEntry entry;
        provider.Catalog(a.arg, entry);
        if (entry.count > 32 || (a.arg < entry.count && !engine_id_ok(entry.id))) return false;
        ui_reply_head(a, entry.revision, r);
        r.packet[n++] = entry.count; r.packet[n++] = entry.state;
        memcpy(r.packet + n, entry.id, 32); n += 32;
        memcpy(r.packet + n, entry.name, 10); n += 10;
        memcpy(r.packet + n, entry.short_name, 4); n += 4;
    } else {
        um::NativeDescriptor d;
        provider.Descriptor(a.id, d);
        ui_reply_head(a, d.revision, r);
        if (a.op == UiMeta) {
            uint8_t meta[RMP_MSG_MAX];
            const int len = a.arg ? rmp_build_meta_part(meta, 1, 0, a.arg, &d.desc) :
                rmp_build_meta_head(meta, 1, 0, d.state, d.flags, &d.desc, 0);
            if (len < 10 || unsigned(len - 10) > kUiReplyMax - kUiHeader - 1) return false;
            // The existing manifest protocol's payload, without its slot/gen/part wrapper.
            memcpy(r.packet + n, meta + 9, unsigned(len - 10)); n += unsigned(len - 10);
        } else {
            char names[RMP_ENUM_MAX][8]{};
            const int total = provider.Names(a.id, unsigned(a.arg) + 1, names);
            if (total < 0 || total > RMP_ENUM_MAX || a.first > total) return false;
            const unsigned count = unsigned(total) - a.first < 8 ? unsigned(total) - a.first : 8;
            r.packet[n++] = uint8_t(total); r.packet[n++] = uint8_t(count);
            memcpy(r.packet + n, names[a.first], count * 8); n += count * 8;
        }
    }
    r.packet[n++] = 0xf7; r.bytes = uint16_t(n);
    if (!ui_reply_ok(r, epoch, token)) return false;
    out = r;
    return true;
}
} // namespace dn2_native
