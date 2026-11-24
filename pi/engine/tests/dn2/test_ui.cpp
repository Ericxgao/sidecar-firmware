#include "dn2_ui.h"
#include "rmp_meta.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <string>

using namespace dn2_native;
static bool rd(void* ctx, uint32_t offset, uint8_t* out, uint32_t n);
struct Provider {
    int track=-1;unsigned selector=0;
    void Context(int t,unsigned s) {track=t;selector=s;}
    rmp_desc d{};
    char id[32] = "demo.test";
    uint32_t revision = 0xfedcba98;
    unsigned calls = 0;
    std::vector<uint8_t>* package = nullptr;
    unsigned loads = 0;
    unsigned Load(unsigned track, const char* engine) {
        ++loads;
        return track < 8 && !strcmp(id, engine) ? RMP_PST_READY : RMP_PST_BAD;
    }
    void Descriptor(const char* engine, um::NativeDescriptor& out) {
        ++calls;
        out.desc = d; out.revision = revision;
        out.state = !strcmp(engine, id) ? RMP_PST_READY : RMP_PST_MISSING;
        out.flags = RMP_FLAG_PARAMS;
    }
    void Catalog(unsigned i, um::NativeCatalogEntry& out) {
        ++calls;
        out = {}; out.count = 1; out.revision = revision;
        if (!i) {
            memcpy(out.id, id, 32); memcpy(out.name, d.name, 11);
            memcpy(out.short_name, d.short_name, 5); out.state = RMP_PST_READY;
        }
    }
    int Names(const char*, unsigned p1, char names[][8]) {
        ++calls;
        if (package) {
            const int q = rmp_desc_find(&d, int(p1));
            return q < 0 ? 0 : rmp_manifest_enum(&d, q, rd, package, names);
        }
        for (unsigned i = 0; i < 36; ++i) snprintf(names[i], 8, "MODE%02u", i);
        return 36;
    }
};
static bool rd(void* ctx, uint32_t offset, uint8_t* out, uint32_t n) {
    auto& data = *static_cast<std::vector<uint8_t>*>(ctx);
    if (offset > data.size() || n > data.size() - offset) return false;
    memcpy(out, data.data() + offset, n); return true;
}
int main(int argc, char** argv) {
    Provider p;
    if (argc == 5 && !strcmp(argv[1], "--answer")) {
        // Compiled ColdFire client interoperability; this process has no device I/O.
        std::ifstream package(argv[4], std::ios::binary); assert(package);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(package), {}};
        rmp_info info{}; rmp_host_target = RMP_TARGET_A64;
        assert(rmp_verify(data.data(), uint32_t(data.size()), &info) == RMP_OK);
        assert(rmp_manifest_read(&info, rd, &data, &p.d));
        p.package = &data; memcpy(p.id, info.id, 32);
        p.d.page2 = 1; rmp_desc_pad_unused(&p.d);
        p.revision = rmp_meta_print(RMP_PST_READY, RMP_FLAG_PARAMS, p.id, info.version, &p.d) ^ info.crc32;
        std::ifstream input(argv[2], std::ios::binary); assert(input);
        std::vector<uint8_t> ask{std::istreambuf_iterator<char>(input), {}};
        UiAsk a; assert(ui_decode_ask(ask.data(), ask.size(), a));
        UiReply reply; assert(ui_answer(p, ask.data(), ask.size(), a.token, 9, reply));
        std::ofstream output(argv[3], std::ios::binary); assert(output);
        output.write(reinterpret_cast<const char*>(reply.packet), reply.bytes);
        return 0;
    }
    strcpy(p.d.name, "TEST"); strcpy(p.d.short_name, "TEST"); strcpy(p.d.p2_title, "SECOND PAGE");
    p.d.page2 = 1;
    rmp_desc_pad_unused(&p.d);
    for (unsigned k = 0; k < 16; ++k) {
        auto& q = p.d.knob[k]; q.p = uint8_t(k + 1); q.min = 0; q.max = 127; q.def = uint8_t(10 + k);
        snprintf(q.short_name, 5, "P%02u", k + 1); snprintf(q.long_name, 13, "PARAM %02u", k + 1);
    }
    UiAsk a; a.token = 0x0fedcba9; a.serial = 0x07654321; strcpy(a.id, p.id);
    uint8_t request[50]; assert(ui_encode_ask(a, request, sizeof request));
    UiAsk parsed; assert(ui_decode_ask(request, sizeof request, parsed));
    assert(parsed.token == a.token && parsed.serial == a.serial && !strcmp(parsed.id, a.id));
    UiReply reply;
    for (unsigned part = 0; part <= 4; ++part) {
        a.arg = uint8_t(part); assert(ui_encode_ask(a, request, sizeof request));
        assert(ui_answer(p, request, sizeof request, a.token, 9, reply));
        assert(ui_reply_ok(reply, 9, a.token));
        assert(!ui_reply_ok(reply, 8, a.token) && !ui_reply_ok(reply, 9, a.token - 1));
        assert(ui_get7(reply.packet + 17, 5) == p.revision);
        uint8_t original[RMP_MSG_MAX];
        int n = part ? rmp_build_meta_part(original, 1, 0, int(part), &p.d) :
            rmp_build_meta_head(original, 1, 0, RMP_PST_READY, RMP_FLAG_PARAMS, &p.d, 0);
        assert(reply.bytes == kUiHeader + unsigned(n - 10) + 1);
        assert(!memcmp(reply.packet + kUiHeader, original + 9, unsigned(n - 10)));
    }
    // Every byte of a valid request: high bits must reject, before touching the provider/output.
    for (unsigned k = 0; k < 50; ++k) {
        uint8_t bad[50]; memcpy(bad, request, 50); bad[k] ^= 128;
        const unsigned calls = p.calls; const UiReply before = reply;
        assert(!ui_answer(p, bad, 50, a.token, 9, reply));
        assert(calls == p.calls && !memcmp(&before, &reply, sizeof reply));
    }
    assert(!ui_answer(p, request, 50, a.token - 1, 9, reply));
    a.op = UiNames; a.arg = 15;
    for (unsigned first = 0; first <= 32; first += 8) {
        a.first = uint8_t(first); assert(ui_encode_ask(a, request, 50));
        assert(ui_answer(p, request, 50, a.token, 9, reply));
        assert(reply.packet[22] == 36 && reply.packet[23] == (first == 32 ? 4 : 8));
        char name[8]; snprintf(name, 8, "MODE%02u", first);
        assert(!memcmp(reply.packet + 24, name, 7));
    }
    a.first = 1; assert(!ui_encode_ask(a, request, 50));
    // Contextual metadata remains bounded, and legacy 50-byte requests still work.
    {
        UiAsk context=a;context.version=3;context.track=7;context.selector=127;context.first=0;context.arg=0;context.op=UiMeta;
        uint8_t contextual[kUiContextAskBytes]{};UiAsk decoded;
        assert(!ui_encode_ask(context,contextual,50));
        assert(ui_encode_ask(context,contextual,sizeof contextual));
        assert(ui_decode_ask(contextual,sizeof contextual,decoded));
        assert(decoded.track==7 && decoded.selector==127 && decoded.version==3);
        assert(ui_answer(p,contextual,sizeof contextual,context.token,9,reply));
        assert(p.track==7 && p.selector==127 && reply.packet[4]==3 && ui_reply_ok(reply,9,context.token));
        contextual[17]=8;assert(!ui_decode_ask(contextual,sizeof contextual,decoded));
        contextual[17]=7;contextual[18]=128;assert(!ui_decode_ask(contextual,sizeof contextual,decoded));
        context.op=UiLoad;assert(!ui_encode_ask(context,contextual,sizeof contextual));
    }
    a.first = 0; a.arg = 16; assert(!ui_encode_ask(a, request, 50));
    a.arg = 0; a.id[31] = 'x'; assert(!ui_encode_ask(a, request, 50)); a.id[31] = 0;
    a.op = UiLoad;
    assert(ui_encode_ask(a, request, 50));
    assert(!ui_answer(p, request, 50, a.token - 1, 9, reply) && p.loads == 0);
    assert(ui_answer(p, request, 50, a.token, 9, reply) && p.loads == 1);
    assert(reply.bytes == 24 && reply.packet[22] == RMP_PST_READY);
    a.arg = 8; assert(ui_encode_ask(a, request, 50));
    assert(ui_answer(p, request, 50, a.token, 9, reply) && reply.packet[22] == RMP_PST_BAD);
    a.op = UiCatalog; memset(a.id, 0, 32);
    for (unsigned i = 0; i < 32; ++i) {
        a.arg = uint8_t(i); assert(ui_encode_ask(a, request, 50));
        assert(ui_answer(p, request, 50, a.token, 9, reply));
        assert(reply.bytes == 71 && reply.packet[22] == 1);
        assert(!strcmp(reinterpret_cast<char*>(reply.packet + 24), i ? "" : p.id));
    }
    // Existing package descriptors use the same builders; no handwritten engine table.
    unsigned packages = 0;
    for (int file = 1; file < argc; ++file) {
        std::ifstream in(argv[file], std::ios::binary); assert(in);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), {}};
        rmp_info info{}; rmp_host_target = RMP_TARGET_A64;
        assert(rmp_verify(data.data(), uint32_t(data.size()), &info) == RMP_OK);
        assert(rmp_manifest_read(&info, rd, &data, &p.d));
        memcpy(p.id, info.id, 32); memcpy(a.id, p.id, 32);
        p.d.page2 = 1; rmp_desc_pad_unused(&p.d);
        p.revision = rmp_meta_print(RMP_PST_READY, RMP_FLAG_PARAMS, p.id, info.version, &p.d) ^ info.crc32;
        a.op = UiMeta; a.first = 0;
        for (unsigned part = 0; part <= 4; ++part) {
            a.arg = uint8_t(part); assert(ui_encode_ask(a, request, 50));
            assert(ui_answer(p, request, 50, a.token, 9, reply));
            assert(ui_reply_ok(reply, 9, a.token));
        }
        ++packages;
    }
    printf("metadata/catalog bounds, sixteen controls, 36 names, stale sessions and %u real packages passed\n", packages);
}
