#include "dn2_native.h"
#include "dn2_receiver.h"
#include "dn2_parts.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using Packet = std::array<uint8_t, 50>;
using Raw = std::array<uint8_t, 37>;
static Packet pack(const Raw& raw) {
    Packet p{0xf0, 0x7d, 0x44, 0x32, 1, 1};
    size_t pos = 6;
    for (size_t i = 0; i < raw.size();) {
        const size_t mask = pos++;
        for (unsigned bit = 0; bit < 7 && i < raw.size(); ++bit, ++i) {
            p[mask] |= uint8_t((raw[i] >> 7) << bit);
            p[pos++] = raw[i] & 127;
        }
    }
    REQUIRE(pos == 49); p[pos] = 0xf7; return p;
}
static Raw fixture() {
    Raw r{0xff, 0xfe, 0x89, 0xab, 0xcd, 0xef, 14, 15, 7, 0x3c, 0x80, 0x64, 0x81, 0x7f, 0};
    for (unsigned i = 0; i < 11; ++i) {
        const unsigned value = (i * 3333 + 171) & 65535;
        r[15 + 2 * i] = uint8_t(value >> 8); r[16 + 2 * i] = uint8_t(value);
    }
    return r;
}
static void verify(const Packet& p) {
    dn2_native::Event e{};
    REQUIRE(dn2_native::decode(p.data(), p.size(), e));
    REQUIRE(e.tick == 0x89abcdef && e.sequence == 0xfffe);
    REQUIRE(e.track == 14 && e.voice == 15 && e.flags == 7);
    REQUIRE(e.note == 0x3c80 && e.velocity == 0x6481 && e.level == 0x7f00);
    for (unsigned i = 0; i < 11; ++i) REQUIRE(e.params[i] == ((i * 3333 + 171) & 65535));
}
struct Sink {
    int ready = 2, slot = -1;
    uint32_t epoch = 1;
    float params[16]{}, note = 0, velocity = 0, level = 0;
    std::string calls;
    int Ready(const char* id) { REQUIRE(std::strcmp(id, "mega.perkons") == 0); return ready; }
    uint32_t Epoch(int) { return epoch; }
    void Params(int s, const float* p, float l) {
        slot = s; std::memcpy(params, p, sizeof params); level = l; calls += 'P';
    }
    void On(float n, float v) { note = n; velocity = v; calls += 'N'; }
    void Off() { calls += 'O'; }
    void Kill() { calls += 'K'; }
    void Request(const char*) {}
    void Release() { Kill(); }
};
struct PartSink {
    Sink voices[8];
    int Ready(const char* id) { return voices[0].Ready(id); }
    uint32_t Epoch(int slot) { return voices[0].Epoch(slot); }
    void Params(unsigned part, int slot, const float* p, float level) { voices[part].Params(slot, p, level); }
    void On(unsigned part, float note, float velocity) { voices[part].On(note, velocity); }
    void Off(unsigned part) { voices[part].Off(); }
    void Kill(unsigned part) { voices[part].Kill(); }
    void Request(unsigned part,const char* id) { voices[part].Request(id); }
    void Release(unsigned part) { voices[part].Release(); }
    void ClearCalls() { for (auto& v : voices) v.calls.clear(); }
};
static void parts_tests() {
    PartSink sink;
    dn2_native::Parts<PartSink> bank(sink);
    uint16_t seq = 65530;
    auto send = [&](unsigned track, unsigned voice, unsigned flags) {
        Raw r = fixture();
        r[0] = uint8_t(seq >> 8); r[1] = uint8_t(seq++);
        r[6] = uint8_t(track); r[7] = uint8_t(voice); r[8] = uint8_t(flags);
        r[9] = uint8_t(36 + track); r[10] = 0;
        r[15] = uint8_t(track); r[16] = 17;
        auto packet = pack(r); return bank.Receive(packet.data(), packet.size());
    };
    REQUIRE(!bank.Begin(17) && !bank.Enabled());
    REQUIRE(bank.Begin(dn2_native::kAllTracks) && bank.Count() == 8);
    REQUIRE(send(0, 0, dn2_native::Reset)); sink.ClearCalls();
    // Deliberately nonidentity DN2 allocated voice slots; interleaved global sequence wraps.
    for (unsigned part = 0; part < 8; ++part) REQUIRE(send(part, 15 - part, 3));
    for (unsigned part = 0; part < 8; ++part) {
        REQUIRE(sink.voices[part].calls == "KPN");
        REQUIRE(sink.voices[part].note == 36 + part);
        REQUIRE(sink.voices[part].params[0] == part + 17 / 256.0f);
        REQUIRE(bank.Part(part).Owner() == int(15 - part) && bank.Part(part).gaps == 0);
    }
    sink.ClearCalls(); REQUIRE(send(4, 11, 1)); // one part's lock does not touch any other part
    for (unsigned part = 0; part < 8; ++part) REQUIRE(sink.voices[part].calls == (part == 4 ? "P" : ""));
    sink.ClearCalls(); REQUIRE(send(4, 2, 3));
    REQUIRE(sink.voices[4].calls == "KPN" && bank.Part(4).Owner() == 2);
    sink.ClearCalls(); REQUIRE(send(4, 11, 8)); REQUIRE(send(4, 11, 5));
    REQUIRE(send(9, 3, 3));
    for (auto& v : sink.voices) REQUIRE(v.calls.empty()); // stale owner and excluded track
    REQUIRE(send(3, 12, 11)); REQUIRE(sink.voices[3].calls == "PK");
    sink.ClearCalls(); ++seq; REQUIRE(send(9, 3, 1)); // lost global packet invalidates every owner
    for (unsigned part = 0; part < 8; ++part) {
        REQUIRE(sink.voices[part].calls == "K" && bank.Part(part).Owner() == -1);
        REQUIRE(bank.Part(part).gaps == 1);
    }
    sink.ClearCalls();
    for (unsigned part = 0; part < 8; ++part) REQUIRE(send(part, part, 1));
    for (auto& v : sink.voices) REQUIRE(v.calls == "KP"); // recall applies controls without phantom notes
    sink.ClearCalls(); REQUIRE(send(0, 0, 16));
    for (auto& v : sink.voices) REQUIRE(v.calls == "K");
    sink.ClearCalls(); bank.End();
    for (auto& v : sink.voices) REQUIRE(v.calls == "K");
    REQUIRE(bank.Count() == 0 && !send(0, 0, 3));
    // Legacy selection still maps any one DN2 track to Pi part zero.
    REQUIRE(bank.Begin(12) && bank.Count() == 1); sink.ClearCalls();
    REQUIRE(send(12, 6, 3)); REQUIRE(sink.voices[0].calls == "KPN");
    for (unsigned part = 1; part < 8; ++part) REQUIRE(sink.voices[part].calls.empty());
}
static void receiver_tests() {
    Sink sink;
    dn2_native::Receiver<Sink> rx(sink);
    uint16_t seq = 65530;
    auto send = [&](unsigned flags, unsigned voice = 3, unsigned track = 7) {
        Raw r = fixture();
        r[0] = uint8_t(seq >> 8); r[1] = uint8_t(seq++);
        r[6] = uint8_t(track); r[7] = uint8_t(voice); r[8] = uint8_t(flags);
        auto p = pack(r); return rx.Receive(p.data(), p.size());
    };
    REQUIRE(!send(3) && sink.calls.empty()); // handshake required
    REQUIRE(!rx.Begin(16) && !rx.Enabled());
    REQUIRE(rx.Begin(7));
    REQUIRE(send(1) && sink.calls == "KP" && rx.Owner() == 3); // no phantom hit
    REQUIRE(sink.slot == 2 && sink.params[0] == 171 / 256.0f);
    REQUIRE(sink.params[10] == ((10 * 3333 + 171) & 65535) / 256.0f);
    REQUIRE(sink.params[11] == 0 && sink.params[15] == 0 && sink.level == 1);
    sink.calls.clear(); REQUIRE(send(7) && sink.calls == "PON");
    REQUIRE(sink.note == 0x3c80 / 256.0f && sink.velocity == 0x6481 / 32512.0f);
    sink.calls.clear(); REQUIRE(send(3, 8) && sink.calls == "KPN" && rx.Owner() == 8);
    sink.calls.clear(); REQUIRE(send(8, 3) && sink.calls.empty()); // stale owner
    REQUIRE(send(5, 3) && sink.calls.empty());
    REQUIRE(send(3, 8, 6) && sink.calls.empty()); // other track, across sequence wrap
    REQUIRE(rx.gaps == 0);
    sink.calls.clear(); REQUIRE(send(5, 8) && sink.calls == "PO"); // natural tail
    sink.calls.clear(); REQUIRE(send(11, 8) && sink.calls == "PK" && rx.Owner() == -1); // muted ON cannot hit
    sink.calls.clear(); REQUIRE(send(3, 9) && sink.calls == "PN");
    sink.calls.clear(); seq += 2; REQUIRE(send(3, 9) && sink.calls == "KKPN" && rx.gaps == 1);
    sink.calls.clear(); --seq; REQUIRE(!send(3, 9) && sink.calls.empty() && rx.stale == 1);
    const auto next = seq; seq -= 2;
    REQUIRE(!send(3, 9) && sink.calls.empty() && rx.stale == 2); seq = next;
    sink.calls.clear(); REQUIRE(send(16) && sink.calls == "K" && rx.Owner() == -1);
    sink.calls.clear(); REQUIRE(send(1, 9) && sink.calls == "KP");
    sink.calls.clear(); sink.ready = -1;
    REQUIRE(send(3, 9) && sink.calls == "K" && rx.unavailable == 1);
    sink.calls.clear(); sink.ready = 7;
    REQUIRE(send(3, 9) && sink.calls == "KPN" && sink.slot == 7);
    sink.calls.clear(); ++sink.epoch;
    REQUIRE(send(1, 9) && sink.calls == "KP"); // reload takes fresh state, no hit
    sink.calls.clear(); REQUIRE(!rx.Begin(99) && rx.Enabled() && sink.calls.empty());
    rx.End(); REQUIRE(sink.calls == "K" && !rx.Enabled());
    sink.calls.clear(); REQUIRE(!send(3) && sink.calls.empty());
    // Every native track can own the single experimental Pi part.
    for (unsigned track = 0; track < 16; ++track) {
        REQUIRE(rx.Begin(track)); sink.calls.clear(); seq = 0;
        REQUIRE(send(3, 15, track) && sink.calls == "KPN");
    }
}
int main(int argc, char** argv) {
    const auto raw = fixture(); const auto good = pack(raw);
    verify(good);
    dn2_native::Event sentinel{}; sentinel.tick = 0x12345678;
    auto reject = [&](const uint8_t* p, size_t n) {
        auto e = sentinel;
        REQUIRE(!dn2_native::decode(p, n, e));
        REQUIRE(e.tick == sentinel.tick && e.sequence == sentinel.sequence && e.flags == sentinel.flags);
    };
    reject(nullptr, 50);
    for (size_t n = 0; n < 70; ++n) if (n != 50) reject(good.data(), n);
    for (size_t i = 1; i < 49; ++i) { auto p = good; p[i] |= 128; reject(p.data(), p.size()); }
    for (size_t i : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(4), size_t(5), size_t(49)}) {
        auto p = good; p[i] ^= 1; reject(p.data(), p.size());
    }
    { auto p = good; p[46] |= 4; reject(p.data(), p.size()); }
    for (unsigned flags = 0; flags < 256; ++flags) {
        auto r = raw; r[8] = uint8_t(flags); auto p = pack(r); dn2_native::Event e{};
        const bool valid = flags == 16 || (flags > 0 && flags < 16 && (!(flags & 6) || (flags & 1)));
        REQUIRE(dn2_native::decode(p.data(), p.size(), e) == valid);
    }
    for (unsigned field : {6u, 7u}) for (unsigned value = 16; value < 256; ++value) {
        auto r = raw; r[field] = uint8_t(value); auto p = pack(r); reject(p.data(), p.size());
    }
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); REQUIRE(f);
        Packet p{}; REQUIRE(std::fread(p.data(), 1, p.size(), f) == p.size());
        REQUIRE(std::fgetc(f) == EOF); std::fclose(f);
        REQUIRE(p == good); verify(p);
        std::puts("compiled ColdFire encoder interoperability passed");
    }
    std::puts("native decoder: full precision, bounds, framing, flags and identities passed");
    receiver_tests();
    parts_tests();
    std::puts("eight parts: independent locks/notes/owners, sequence wrap/loss, mute, recall snapshots and teardown passed");
    std::puts("native receiver: ownership, order, loss, reset, plugin identity and reload passed");
}
