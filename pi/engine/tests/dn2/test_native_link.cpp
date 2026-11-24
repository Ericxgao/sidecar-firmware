#include "dn2_link.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace dn2_native;
using Packet = std::array<uint8_t, kLinkBytes>;

int main(int argc, char** argv) {
    Packet p{};
    for (unsigned mode = 0; mode < 2; ++mode)
    for (unsigned cmd = 0; cmd < 4; ++cmd) for (unsigned track = 0; track <= 16; ++track)
        for (uint32_t token : {1u, 127u, 128u, 0x1234567u, 0xfffffffu}) {
            LinkControl in{LinkCommand(cmd), token, uint8_t(track), uint8_t(mode)}, out{};
            REQUIRE(encode_link(in, p.data(), p.size()));
            REQUIRE(decode_link(p.data(), p.size(), out));
            REQUIRE(out.command == in.command && out.token == token && out.track == track && out.returns == mode);
        }
    const LinkControl ack{LinkCommand::Ack, 0x1234567, 2};
    REQUIRE(encode_link(ack, p.data(), p.size()));
    const Packet good = p;
    auto reject = [&](const uint8_t* data, size_t size) {
        LinkControl out{LinkCommand::Close, 7, 9};
        REQUIRE(!decode_link(data, size, out));
        REQUIRE(out.command == LinkCommand::Close && out.token == 7 && out.track == 9);
    };
    reject(nullptr, 50);
    for (size_t size = 0; size < 70; ++size) if (size != 50) reject(p.data(), size);
    for (size_t i = 1; i < 49; ++i) { auto bad = good; bad[i] |= 128; reject(bad.data(), bad.size()); }
    for (size_t i : {0u, 1u, 2u, 3u, 4u, 5u, 12u, 49u}) {
        auto bad = good; bad[i] ^= 1; reject(bad.data(), bad.size());
    }
    for (size_t i = 14; i < 49; ++i) { auto bad = good; bad[i] = 1; reject(bad.data(), bad.size()); }
    { auto bad = good; bad[13] = 2; reject(bad.data(), bad.size()); }
    for (unsigned cmd = 4; cmd < 128; ++cmd) { auto bad = good; bad[6] = uint8_t(cmd); reject(bad.data(), bad.size()); }
    for (unsigned track = 17; track < 128; ++track) { auto bad = good; bad[11] = uint8_t(track); reject(bad.data(), bad.size()); }
    auto zero = good; for (unsigned i = 7; i < 11; ++i) zero[i] = 0; reject(zero.data(), zero.size());
    // Invalid values and insufficient output capacity leave the whole buffer unchanged.
    for (LinkControl bad : {LinkControl{LinkCommand(4), 1, 0}, {LinkCommand::Hello, 0, 0},
                           {LinkCommand::Hello, 0x10000000, 0}, {LinkCommand::Hello, 1, 17}, {LinkCommand::Hello, 1, 0, 2}}) {
        REQUIRE(!encode_link(bad, p.data(), p.size()) && p == good);
    }
    for (size_t n = 0; n < 50; ++n) REQUIRE(!encode_link(ack, p.data(), n) && p == good);
    REQUIRE(!encode_link(ack, nullptr, 50));
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); REQUIRE(f);
        Packet actual{}; REQUIRE(std::fread(actual.data(), 1, actual.size(), f) == actual.size());
        REQUIRE(std::fgetc(f) == EOF); std::fclose(f);
        REQUIRE(actual == good);
        LinkControl out{}; REQUIRE(decode_link(actual.data(), actual.size(), out));
        REQUIRE(out.command == LinkCommand::Ack && out.token == ack.token && out.track == ack.track);
        std::puts("compiled ColdFire session ACK interoperability passed");
    }
    std::puts("native link codec: all tracks, commands, nonce bounds, framing and reserved bytes passed");
}
