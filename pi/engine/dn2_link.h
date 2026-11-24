// Native DN2 session control. USB transport only; distinct from engine reports
// and the sampler's protocol. Codec only: decoding is not permission to take
// over the stock MIDI adapter; the live session must match nonce and track.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dn2_native {
enum class LinkCommand : uint8_t { Hello = 0, Ack = 1, Keep = 2, Close = 3 };
struct LinkControl {
    LinkCommand command = LinkCommand::Hello;
    uint32_t token = 0; // nonzero 28-bit session nonce, not authentication
    uint8_t track = 0;
    uint8_t returns = 0; // 0 legacy stereo, 1 eight dry mono lanes into native DSP
};
constexpr size_t kLinkBytes = 50;
constexpr uint32_t kLinkLeaseMs = 2000, kLinkRenewMs = 500;

inline bool decode_link(const uint8_t* p, size_t bytes, LinkControl& out) {
    if (!p || bytes != kLinkBytes || p[0] != 0xf0 || p[1] != 0x7d ||
        p[2] != 0x44 || p[3] != 0x32 || p[4] != 1 || p[5] != 2 ||
        p[49] != 0xf7 || p[6] > 3 || p[11] > 16 || p[12] != 1 || p[13] > 1) return false;
    for (size_t i = 6; i < 49; ++i)
        if ((p[i] & 128) || (i >= 14 && p[i])) return false;
    const uint32_t token = (uint32_t(p[7]) << 21) | (uint32_t(p[8]) << 14) |
                           (uint32_t(p[9]) << 7) | p[10];
    if (!token) return false;
    out = {LinkCommand(p[6]), token, p[11], p[13]};
    return true;
}

inline bool encode_link(const LinkControl& in, uint8_t* p, size_t capacity) {
    if (!p || capacity < kLinkBytes || unsigned(in.command) > 3 ||
        !in.token || in.token > 0x0fffffff || in.track > 16 || in.returns > 1) return false;
    for (size_t i = 0; i < kLinkBytes; ++i) p[i] = 0;
    p[0] = 0xf0; p[1] = 0x7d; p[2] = 0x44; p[3] = 0x32; p[4] = 1; p[5] = 2;
    p[6] = uint8_t(in.command);
    p[7] = uint8_t((in.token >> 21) & 127); p[8] = uint8_t((in.token >> 14) & 127);
    p[9] = uint8_t((in.token >> 7) & 127); p[10] = uint8_t(in.token & 127);
    p[11] = in.track; p[12] = 1; p[13] = in.returns; p[49] = 0xf7;
    return true;
}
} // namespace dn2_native
