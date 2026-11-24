// Native DN2 events: legacy Perkons v1 and generic sixteen-control v2.
// No allocation, device I/O, DSP state or dependence on C structure padding.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dn2_native {
enum : uint8_t { State = 1, On = 2, Off = 4, Kill = 8, Reset = 16 };
constexpr size_t PacketBytes = 50;
constexpr size_t GenericPacketBytes = 98, EngineIdBytes = 32;
struct Event {
    uint32_t tick;
    uint16_t sequence, note, velocity, level, params[16];
    uint8_t track, voice, flags;
    char engine[EngineIdBytes]; // full manifest ID, NUL-padded, not a Pi slot
};
// Same ID alphabet as the existing plugin loader, with canonical zero padding.
inline bool engine_id_ok(const char* id) {
    if (!id[0] || id[0] == '.') return false;
    bool end = false;
    for (unsigned i=0;i<EngineIdBytes;++i) {
        const unsigned char c=static_cast<unsigned char>(id[i]);
        if (!c) { end=true; continue; }
        if (end || c<0x21 || c>0x7e || c=='/' || c=='\\' || (i && c=='.' && id[i-1]=='.')) return false;
    }
    return end;
}
inline uint16_t read16(const uint8_t* p) {
    return uint16_t(uint16_t(p[0]) << 8 | p[1]);
}
// Failure leaves the caller's event unchanged. All value fraction bits survive.
inline bool decode(const uint8_t* packet, size_t size, Event& result) {
    if (!packet || (size != PacketBytes && size != GenericPacketBytes)) return false;
    const bool generic=packet[4]==2;
    if (packet[0]!=0xf0 || packet[1]!=0x7d || packet[2]!=0x44 || packet[3]!=0x32 ||
        (packet[4]!=1 && !generic) || packet[5]!=1 || packet[size-1]!=0xf7 ||
        size!=(generic?GenericPacketBytes:PacketBytes)) return false;
    for (size_t i=1;i<size-1;++i) if (packet[i]&0x80) return false;
    uint8_t raw[79];
    const unsigned raw_bytes=generic?79:37;
    size_t src = 6, dst = 0;
    while (dst < raw_bytes) {
        const unsigned bits = raw_bytes - dst < 7 ? unsigned(raw_bytes - dst) : 7;
        const uint8_t mask = packet[src++];
        if (mask >> bits) return false; // reject noncanonical padding bits
        for (unsigned bit = 0; bit < bits; ++bit)
            raw[dst++] = uint8_t(packet[src++] | ((mask >> bit & 1) << 7));
    }
    const uint8_t flags = raw[8];
    if (raw[6] >= 16 || raw[7] >= 16 || !flags || flags & ~uint8_t(31) ||
        ((flags & Reset) && flags != Reset) ||
        ((flags & (On | Off)) && !(flags & State))) return false;
    Event e{};
    e.sequence = read16(raw);
    e.tick = uint32_t(read16(raw + 2)) << 16 | read16(raw + 4);
    e.track = raw[6]; e.voice = raw[7]; e.flags = flags;
    e.note = read16(raw + 9); e.velocity = read16(raw + 11); e.level = read16(raw + 13);
    for (unsigned p=0;p<(generic?16u:11u);++p) e.params[p]=read16(raw+15+2*p);
    if (generic) {
        for(unsigned i=0;i<EngineIdBytes;++i)e.engine[i]=char(raw[47+i]);
        if (flags!=Reset && !engine_id_ok(e.engine)) return false;
        if (flags==Reset) for(char c:e.engine) if(c) return false;
    } else {
        constexpr char legacy[]="mega.perkons";
        for(unsigned i=0;i<sizeof legacy;++i)e.engine[i]=legacy[i];
    }
    result = e;
    return true;
}
// v2 serializer shared with synthetic host fixtures; firmware encoder must pass
// byte-for-byte interoperability against this layout. Failure writes nothing.
inline bool encode_generic(const Event& e,uint8_t* packet,size_t capacity) {
    if(!packet||capacity<GenericPacketBytes||e.track>=16||e.voice>=16||!e.flags||
       (e.flags&~uint8_t(31))||((e.flags&Reset)&&e.flags!=Reset)||
       ((e.flags&(On|Off))&&!(e.flags&State)))return false;
    if(e.flags!=Reset&&!engine_id_ok(e.engine))return false;
    if(e.flags==Reset)for(char c:e.engine)if(c)return false;
    uint8_t raw[79]{};
    auto put=[](uint8_t* p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);};
    put(raw,e.sequence);put(raw+2,uint16_t(e.tick>>16));put(raw+4,uint16_t(e.tick));
    raw[6]=e.track;raw[7]=e.voice;raw[8]=e.flags;
    put(raw+9,e.note);put(raw+11,e.velocity);put(raw+13,e.level);
    for(unsigned i=0;i<16;++i)put(raw+15+2*i,e.params[i]);
    for(unsigned i=0;i<EngineIdBytes;++i)raw[47+i]=uint8_t(e.engine[i]);
    packet[0]=0xf0;packet[1]=0x7d;packet[2]=0x44;packet[3]=0x32;packet[4]=2;packet[5]=1;
    unsigned n=6;
    for(unsigned i=0;i<sizeof raw;){const unsigned mask=n++;packet[mask]=0;
        for(unsigned bit=0;bit<7&&i<sizeof raw;++bit,++i){packet[mask]|=uint8_t((raw[i]>>7)<<bit);packet[n++]=raw[i]&127;}}
    packet[n]=0xf7;return true;
}
} // namespace dn2_native
