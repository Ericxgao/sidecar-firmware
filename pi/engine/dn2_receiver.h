// One native DN2 track -> one Pi voice. Control core only, between render blocks.
// Transport must explicitly Begin after the version/session handshake and End
// on disconnect/timeout. No implicit takeover of the stock MIDI adapter.
#pragma once
#include "dn2_native.h"

namespace dn2_native {
// Sink contract (all bounded, no I/O):
// Ready(full_engine_id) -> nonnegative host handle, or -1; Epoch(handle) -> lifecycle.
// Handles may identify built-ins as well as plugin slots; only the sink interprets them.
// Request(full_id) queues loading/reserves this part's identity; Release() retires
// this part's state and reservation. Neither performs file I/O. Kill() only silences.
// Params(slot, float[16], track_level), On(note, normalized_velocity), Off(), Kill().
// Params must apply full precision before On. It must not itself trigger a note.
template<class Sink> class Receiver {
public:
    explicit Receiver(Sink& sink) : sink_(sink) {}
    bool Begin(unsigned track) {
        if (track >= 16) return false;
        End();
        track_ = uint8_t(track); enabled_ = true;
        return true;
    }
    void End() {
        if (enabled_) sink_.Release();
        enabled_ = have_sequence_ = false;
        owner_ = slot_ = -1;
        pending_ = false; engine_[0] = 0;
    }
    bool Receive(const uint8_t* bytes, size_t size) {
        Event e{};
        if (!enabled_ || !decode(bytes, size, e)) return false;
        return Receive(e);
    }
    // Already validated by the packet decoder; lets a part bank decode once.
    bool Receive(const Event& e) {
        if (!enabled_) return false;
        if (have_sequence_) {
            const uint16_t distance = uint16_t(e.sequence - sequence_);
            if (!distance || distance >= 0x8000) { ++stale; return false; }
            if (distance != 1) { ++gaps; Clear(); }
        }
        sequence_ = e.sequence; have_sequence_ = true;
        ++received;
        last = e;
        if (e.flags == Reset) { ++resets; Clear(); return true; }
        if (e.track != track_) return true; // still participates in global sequence
        // A stale old owner's event must not switch the current owner's engine.
        if (!(e.flags&On) && owner_>=0 && owner_!=e.voice) return true;
        // Plain KILL is the DN2's removal/engine-change edge. A muted state is
        // STATE|KILL and retains its assignment for the next hit.
        if (e.flags == Kill) {
            if (engine_[0] && !Same(e.engine)) return true;
            ++kills; last_kill = e;
            sink_.Release(); owner_ = slot_ = -1; engine_[0] = 0; pending_ = false;
            return true;
        }
        const bool changed = !Same(e.engine);
        if (changed) {
            sink_.Release(); owner_ = slot_ = -1;
            for (unsigned i=0;i<EngineIdBytes;++i) engine_[i]=e.engine[i];
        }
        if ((e.flags & On) && owner_ >= 0 && owner_ != e.voice) { ++owner_changes; sink_.Kill(); }
        if ((e.flags & On) || owner_ < 0) owner_ = e.voice;
        sink_.Request(e.engine); // committed identity, never a picker preview
        const int slot = sink_.Ready(e.engine);
        if (slot < 0) {
            ++unavailable;
            if (!changed) sink_.Kill();
            slot_ = -1; pending_ = true; pending_state_ = e;
            return true;
        }
        const uint32_t epoch = sink_.Epoch(slot);
        if (slot != slot_ || epoch != epoch_) {
            if (!changed) sink_.Kill();
            slot_ = slot; epoch_ = epoch;
        }
        // A new ON owns the monophonic part. A late OFF/KILL or a smoothed
        // parameter frame from the old allocated DN2 voice must not change it.
        if (owner_ != e.voice) return true;
        pending_ = false;
        if (e.flags & State) {
            ++states;
            float p[16]{};
            for (unsigned i = 0; i < 16; ++i) p[i] = e.params[i] / 256.0f;
            sink_.Params(slot_, p, e.level / 32512.0f);
        }
        if (e.flags & Kill) { ++kills; last_kill = e; sink_.Kill(); owner_ = -1; return true; }
        if (e.flags & Off) { ++offs; sink_.Off(); }
        if (e.flags & On) { ++ons; sink_.On(e.note / 256.0f, e.velocity / 32512.0f); }
        return true;
    }
    // Loading can finish while the DN2 sends no further edits or notes. Restore
    // its newest controls then, but never replay a note-on that arrived too early.
    void Poll() {
        if (!enabled_ || !pending_) return;
        const int slot = sink_.Ready(engine_);
        if (slot < 0) return;
        slot_ = slot; epoch_ = sink_.Epoch(slot);
        float p[16]{};
        for (unsigned i=0;i<16;++i) p[i]=pending_state_.params[i]/256.0f;
        sink_.Params(slot,p,pending_state_.level/32512.0f);
        if (pending_state_.flags & Kill) { sink_.Kill(); owner_ = -1; }
        pending_ = false;
    }
    bool Enabled() const { return enabled_; }
    int Owner() const { return owner_; }
    uint32_t received = 0, stale = 0, gaps = 0, unavailable = 0;
    // Read on demand by the control core; no logging in the event/render path.
    uint32_t resets = 0, states = 0, kills = 0, offs = 0, ons = 0, owner_changes = 0;
    Event last{}, last_kill{};
private:
    bool Same(const char* id) const {
        for (unsigned i=0;i<EngineIdBytes;++i) if (engine_[i]!=id[i]) return false;
        return true;
    }
    void Clear() { sink_.Kill(); owner_ = slot_ = -1; pending_ = false; }
    Sink& sink_;
    bool enabled_ = false, have_sequence_ = false;
    uint8_t track_ = 0;
    uint16_t sequence_ = 0;
    int owner_ = -1, slot_ = -1;
    uint32_t epoch_ = 0;
    char engine_[EngineIdBytes]{};
    bool pending_ = false;
    Event pending_state_{};
};
} // namespace dn2_native
