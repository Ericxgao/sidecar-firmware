// Eight independent native parts. Decodes each packet once; all active receivers
// observe the global sequence, but only the addressed track changes its voice.
#pragma once
#include "dn2_receiver.h"

namespace dn2_native {
constexpr unsigned kAllTracks = 16, kParts = 8;

// Sink uses the Receiver sink methods with a leading part index for
// Params/On/Off/Kill. Ready and Epoch describe the shared plugin image.
template<class Sink> class Parts {
    struct Adapter {
        Sink& sink;
        unsigned part;
        int Ready(const char* id) { return sink.Ready(id); }
        uint32_t Epoch(int slot) { return sink.Epoch(slot); }
        void Params(int slot, const float* p, float level) { sink.Params(part, slot, p, level); }
        void On(float note, float velocity) { sink.On(part, note, velocity); }
        void Off() { sink.Off(part); }
        void Kill() { sink.Kill(part); }
        void Request(const char* id) { sink.Request(part,id); }
        void Release() { sink.Release(part); }
    };
public:
    explicit Parts(Sink& sink) :
        adapters_{{sink,0},{sink,1},{sink,2},{sink,3},{sink,4},{sink,5},{sink,6},{sink,7}},
        receivers_{Receiver<Adapter>(adapters_[0]),Receiver<Adapter>(adapters_[1]),
                   Receiver<Adapter>(adapters_[2]),Receiver<Adapter>(adapters_[3]),
                   Receiver<Adapter>(adapters_[4]),Receiver<Adapter>(adapters_[5]),
                   Receiver<Adapter>(adapters_[6]),Receiver<Adapter>(adapters_[7])} {}
    Parts(const Parts&) = delete;
    Parts& operator=(const Parts&) = delete;
    bool Begin(unsigned selection) {
        if (selection > kAllTracks) return false;
        End();
        count_ = selection == kAllTracks ? kParts : 1;
        for (unsigned i = 0; i < count_; ++i)
            receivers_[i].Begin(count_ == kParts ? i : selection);
        return true;
    }
    void End() {
        for (auto& r : receivers_) r.End();
        count_ = 0;
    }
    bool Receive(const uint8_t* bytes, size_t size) {
        Event event{};
        if (!count_ || !decode(bytes, size, event)) return false;
        bool accepted = false;
        for (unsigned i = 0; i < count_; ++i)
            accepted = receivers_[i].Receive(event) || accepted;
        return accepted;
    }
    bool Enabled() const { return count_ != 0; }
    void Poll() { for (unsigned i=0;i<count_;++i) receivers_[i].Poll(); }
    unsigned Count() const { return count_; }
    const Receiver<Adapter>& Part(unsigned part) const { return receivers_[part]; }
private:
    Adapter adapters_[kParts];
    Receiver<Adapter> receivers_[kParts];
    unsigned count_ = 0;
};
} // namespace dn2_native
