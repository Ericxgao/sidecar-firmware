// Control-core native session lifecycle. No I/O, allocation or clocks here.
// Endpoint: Send(LinkControl, transport_epoch), Begin(track, returns), End(),
// Event(bytes, size). Begin silences/suspends stock notes; End restores their
// saved machine/parameters without a hit. Poll before dispatching incoming MIDI.
#pragma once
#include "dn2_link.h"
#include "dn2_native.h"

namespace dn2_native {
template<class Endpoint> class Session {
public:
    enum Phase { Idle, Hello, AwaitReset, Active };
    explicit Session(Endpoint& endpoint) : endpoint_(endpoint) {}
    bool Configure(int track, uint32_t seed = 0, uint8_t returns = 0) {
        if (track < -1 || track > 16 || returns > 1) return false;
        if (track == desired_ && returns == returns_) return true;
        Stop(true);
        desired_ = track;
        returns_ = returns;
        if (!token_ && seed) token_ = seed & 0x0fffffff;
        return true;
    }
    void Disconnect() { Stop(false); }
    // Session ownership is the explicit host/USB agreement, not whichever plugin
    // happens to load first. Metadata must also work with no ready machines.
    // Individual receivers validate full identity and their own slot lifecycle.
    void Poll(uint32_t now, bool connected, uint32_t transport_epoch) {
        if (!connected) { Stop(false); return; }
        if (phase_ != Idle && transport_epoch != transport_epoch_)
            Stop(transport_epoch == transport_epoch_);
        if (desired_ < 0) return;
        if (phase_ != Idle && Expired(now)) { Stop(true); return; }
        if (phase_ == Idle) {
            token_ = (token_ + 1) & 0x0fffffff;
            if (!token_) token_ = 1;
            transport_epoch_ = transport_epoch;
            phase_ = Hello; started_ = ack_at_ = now;
            attempted_ = hello_sent_ = false;
        }
        if (!attempted_ || now - sent_at_ >= kLinkRenewMs) {
            const bool sent = endpoint_.Send({phase_ == Hello ? LinkCommand::Hello : LinkCommand::Keep,
                                             token_, uint8_t(desired_), returns_}, transport_epoch_);
            if (phase_ == Hello && sent) hello_sent_ = true;
            attempted_ = true; sent_at_ = now;
        }
    }
    bool Receive(const uint8_t* p, size_t bytes, uint32_t now) {
        if (phase_ == Idle) return false;
        if (Expired(now)) { Stop(true); return false; }
        LinkControl control;
        if (decode_link(p, bytes, control)) {
            if (!hello_sent_ || control.command != LinkCommand::Ack ||
                control.token != token_ || control.track != desired_ || control.returns != returns_) return false;
            if (phase_ == Hello) {
                if (!endpoint_.Begin(unsigned(desired_), returns_)) { Stop(true); return false; }
                phase_ = AwaitReset; started_ = now;
            }
            ack_at_ = now;
            return true;
        }
        if (phase_ == Hello) return false;
        Event event;
        if (!decode(p, bytes, event)) return false;
        // Queue pressure can replace the first RESET with a later recovery
        // RESET. Ordered USB + the matching ACK, not sequence zero, separates
        // sessions. Accept that reset's sequence as the receiver's new baseline.
        if (phase_ == AwaitReset && event.flags != Reset) return false;
        if (!endpoint_.Event(p, bytes)) return false;
        phase_ = Active;
        return true;
    }
    bool OwnsVoice() const { return phase_ == AwaitReset || phase_ == Active; }
    Phase State() const { return phase_; }
    int Track() const { return desired_; }
    uint8_t Returns() const { return returns_; }
    uint32_t Token() const { return token_; }
private:
    bool Expired(uint32_t now) const {
        return now - ack_at_ >= kLinkLeaseMs ||
            ((phase_ == Hello || phase_ == AwaitReset) && now - started_ >= kLinkLeaseMs);
    }
    void Stop(bool close) {
        if (phase_ == Idle) return;
        if (close && hello_sent_)
            endpoint_.Send({LinkCommand::Close, token_, uint8_t(desired_), returns_}, transport_epoch_);
        if (OwnsVoice()) endpoint_.End();
        phase_ = Idle; hello_sent_ = false;
    }
    Endpoint& endpoint_;
    int desired_ = -1;
    Phase phase_ = Idle;
    uint32_t token_ = 0, transport_epoch_ = 0;
    uint32_t started_ = 0, ack_at_ = 0, sent_at_ = 0;
    bool attempted_ = false, hello_sent_ = false;
    uint8_t returns_ = 0;
};
} // namespace dn2_native
