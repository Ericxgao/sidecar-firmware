#include "dn2_session.h"
#include "dn2_transport.h"
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace dn2_native;
using Packet = std::array<uint8_t, 50>;
struct Endpoint {
    bool writable = true, ready = true;
    unsigned begins = 0, ends = 0, events = 0, track = 99, returns = 99;
    std::vector<LinkRequest> sent;
    bool Send(LinkControl control, uint32_t epoch) {
        if (!writable) return false;
        sent.push_back({control, epoch}); return true;
    }
    bool Begin(unsigned t, uint8_t mode) { ++begins; track = t; returns = mode; return ready; }
    void End() { ++ends; }
    bool Event(const uint8_t* p, size_t n) { dn2_native::Event event; REQUIRE(decode(p, n, event)); ++events; return true; }
};
using Link = Session<Endpoint>;
static Packet control(uint32_t token, uint8_t track = 2, LinkCommand cmd = LinkCommand::Ack, uint8_t mode = 0) {
    Packet p{}; REQUIRE(encode_link({cmd, token, track, mode}, p.data(), p.size())); return p;
}
static Packet event(unsigned flags, unsigned seq = 0) {
    std::array<uint8_t, 37> raw{}; raw[0] = uint8_t(seq >> 8); raw[1] = uint8_t(seq); raw[8] = uint8_t(flags);
    Packet p{0xf0, 0x7d, 0x44, 0x32, 1, 1}; size_t at = 6;
    for (size_t i = 0; i < raw.size();) {
        const size_t mask = at++;
        for (unsigned bit = 0; bit < 7 && i < raw.size(); ++bit, ++i) {
            p[mask] |= uint8_t((raw[i] >> 7) << bit); p[at++] = raw[i] & 127;
        }
    }
    p[49] = 0xf7; return p;
}
static bool receive(Link& link, Packet p, uint32_t now) { return link.Receive(p.data(), p.size(), now); }
static void activate(Link& link, uint32_t now = 0) {
    REQUIRE(link.Configure(2, 123)); link.Poll(now, true, 7);
    REQUIRE(receive(link, control(link.Token()), now));
    REQUIRE(link.State() == Link::AwaitReset && link.OwnsVoice());
    REQUIRE(receive(link, event(Reset), now)); REQUIRE(link.State() == Link::Active);
}
int main() {
    {
        // Boot can precede USB attachment. Native startup must wait for the
        // matching host, survive a silent/stock host, then reconnect by itself.
        Endpoint ep; Link link(ep);
        REQUIRE(link.Configure(16, 123, 1));
        link.Poll(0, false, 0); REQUIRE(ep.sent.empty() && !link.OwnsVoice());
        link.Poll(100, true, 7);
        link.Poll(2100, true, 7); // no ACK: do not steal stock MIDI voices
        REQUIRE(ep.begins == 0 && !link.OwnsVoice());
        link.Poll(2101, true, 7);
        REQUIRE(receive(link, control(link.Token(), 16, LinkCommand::Ack, 1), 2102));
        REQUIRE(receive(link, event(Reset), 2103));
        const auto old_token = link.Token();
        link.Disconnect(); link.Poll(2200, false, 7);
        REQUIRE(link.Track() == 16 && link.Returns() == 1 && ep.ends == 1);
        link.Poll(2300, true, 8);
        REQUIRE(!receive(link, control(old_token, 16, LinkCommand::Ack, 1), 2301));
        REQUIRE(receive(link, control(link.Token(), 16, LinkCommand::Ack, 1), 2302));
        REQUIRE(receive(link, event(Reset), 2303));
        REQUIRE(ep.begins == 2 && link.State() == Link::Active);
        REQUIRE(link.Configure(-1)); // manual off stays off until next boot
        const auto sent = ep.sent.size(); link.Poll(3000, true, 8);
        REQUIRE(ep.sent.size() == sent && !link.OwnsVoice());
    }
    {
        Endpoint ep; Link link(ep);
        link.Poll(0, true, 7); REQUIRE(ep.sent.empty()); // opt-in required
        REQUIRE(!link.Configure(17) && !link.Configure(-2) && !link.Configure(0, 0, 2));
        REQUIRE(link.Configure(2, 123));
        link.Poll(0, false, 7); REQUIRE(ep.sent.empty());
        link.Poll(0, true, 7);
        REQUIRE(ep.sent.size() == 1 && ep.sent[0].epoch == 7 && ep.sent[0].control.command == LinkCommand::Hello);
        REQUIRE(!link.OwnsVoice() && ep.begins == 0);
        REQUIRE(!receive(link, event(State | On), 1));
        REQUIRE(!receive(link, control(link.Token() + 1), 1));
        REQUIRE(!receive(link, control(link.Token(), 3), 1));
        REQUIRE(!receive(link, control(link.Token(), 2, LinkCommand::Hello), 1));
        link.Poll(499, true, 7); REQUIRE(ep.sent.size() == 1);
        link.Poll(500, true, 7); REQUIRE(ep.sent.size() == 2);
        REQUIRE(receive(link, control(link.Token()), 501));
        REQUIRE(ep.begins == 1 && ep.track == 2 && link.OwnsVoice());
        REQUIRE(!receive(link, event(State | On), 502));
        REQUIRE(ep.events == 0);
        REQUIRE(receive(link, event(Reset, 12), 503)); // recovery may have superseded initial sequence zero
        REQUIRE(receive(link, event(State | On, 1), 504) && ep.events == 2);
        REQUIRE(receive(link, control(link.Token()), 505) && ep.begins == 1);
        link.Poll(1000, true, 7); REQUIRE(ep.sent.back().control.command == LinkCommand::Keep);
        REQUIRE(link.Configure(-1) && ep.ends == 1 && !link.OwnsVoice());
        REQUIRE(ep.sent.back().control.command == LinkCommand::Close);
        REQUIRE(!receive(link, event(State | On, 2), 1001));
    }
    for (uint32_t start : {0u, 0xfffffc00u}) {
        Endpoint ep; Link link(ep); activate(link, start);
        const auto token = link.Token();
        // Voice traffic cannot renew the lease; only matching ACKs can.
        REQUIRE(receive(link, event(State, 1), start + 1999));
        REQUIRE(!receive(link, control(token), start + 2000));
        REQUIRE(ep.ends == 1 && !link.OwnsVoice());
        link.Poll(start + 2001, true, 7); REQUIRE(link.Token() != token);
        REQUIRE(!receive(link, control(token), start + 2002));
    }
    {
        Endpoint ep; Link link(ep); link.Configure(2); link.Poll(0, true, 7);
        REQUIRE(receive(link, control(link.Token()), 100));
        for (unsigned now : {500u, 1000u, 1500u, 2000u}) REQUIRE(receive(link, control(link.Token()), now));
        link.Poll(2100, true, 7); // ACKs cannot keep a missing initial RESET waiting forever
        REQUIRE(ep.ends == 1 && link.State() == Link::Idle);
    }
    for (unsigned change = 0; change < 2; ++change) {
        Endpoint ep; Link link(ep); activate(link); const auto token = link.Token();
        if (change == 0) link.Poll(1, false, 7);
        if (change == 1) link.Poll(1, true, 8);
        REQUIRE(ep.ends == 1 && !link.OwnsVoice());
        REQUIRE(!receive(link, control(token), 2));
    }
    {
        Endpoint ep; Link link(ep); ep.writable = false;
        link.Configure(2, 0xfffffff); link.Poll(0, true, 7);
        REQUIRE(link.Token() == 1 && !receive(link, control(1), 1)); // failed queue cannot authorize ACK
        ep.writable = true; link.Poll(500, true, 7);
        ep.ready = false; REQUIRE(!receive(link, control(1), 501));
        REQUIRE(!link.OwnsVoice() && ep.ends == 0);
    }
    {
        Endpoint ep; Link link(ep); activate(link);
        link.Disconnect(); link.Disconnect(); REQUIRE(ep.ends == 1);
        REQUIRE(link.Configure(9)); link.Poll(2, true, 9);
        REQUIRE(receive(link, control(link.Token(), 9), 3)); REQUIRE(ep.track == 9);
    }
    {
        Endpoint ep; Link link(ep);
        REQUIRE(link.Configure(16, 123, 1)); link.Poll(0, true, 7);
        REQUIRE(ep.sent.back().control.track == 16 && ep.sent.back().control.returns == 1);
        REQUIRE(!receive(link, control(link.Token(), 16), 1)); // stereo ACK cannot enable native lanes
        REQUIRE(receive(link, control(link.Token(), 16, LinkCommand::Ack, 1), 2));
        REQUIRE(ep.track == 16 && ep.returns == 1 && ep.begins == 1);
        REQUIRE(receive(link, event(Reset), 3));
        REQUIRE(link.Configure(16, 0, 0) && ep.ends == 1);
        REQUIRE(ep.sent.back().control.command == LinkCommand::Close && ep.sent.back().control.returns == 1);
        link.Poll(4, true, 7);
        REQUIRE(!receive(link, control(link.Token(), 16, LinkCommand::Ack, 1), 5));
        REQUIRE(receive(link, control(link.Token(), 16), 6));
        REQUIRE(ep.returns == 0 && ep.begins == 2);
    }
    std::puts("native session: opt-in, eight-track/mode negotiation, ACK/RESET ordering, renewal, timeout/wrap, stale replies, plugin-independent ownership and reconnect passed");
}
