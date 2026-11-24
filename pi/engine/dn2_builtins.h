// Built-in synth identity/metadata for the native host. No DSP or plugin slots.
// IDs and P positions are saved in DN2 sounds: keep them stable.
#pragma once
#include "user/native_catalog.h"
#include "rmp_proto.h"
#include "rmp_meta.h"
#include <string.h>

namespace dn2_native { namespace builtin {
constexpr int First = 16, Count = 5;
struct Synth {
    const char* id; const char* name; const char* short_name;
    unsigned flag;
    const char* labels[16];
    uint8_t defaults[16];
};
static const Synth synths[Count] = {
    {"sidecar.builtin.plaits", "PLAITS", "PLTS", 0x00,
     {"ENG","HRM","TIM","MOR","DEC","LPG","TUN","","FM","FIN","AUX","TMD","MMD","VEL","GLD",""},
     {40,64,64,64,64,64,64,100,64,64,0,0,0,120,0,100}},
    {"sidecar.builtin.juno", "JUNO", "JUNO", 0x08,
     {"PAT","CUT","RES","ENV","ATK","DEC","REL","LEN"},
     {0,80,20,64,10,64,64,127}},
    {"sidecar.builtin.daisy", "DAISYSP", "DSY", 0x10,
     {"ENG","STR","BRI","DMP","","EXC","TUN"},
     {0,64,64,64,0,64,64,100,64,64,0,0,0,120,0,100}},
    {"sidecar.builtin.fm", "6-OP FM", "6FM", 0x48,
     {"PAT","TIM","MOR","DEC","LPG","TUN","","LEN"},
     {0,64,64,64,64,64,100,127}},
    // Legacy requests keep neutral labels; contextual requests replace these
    // with the selected model's names without changing the saved P positions.
    {"sidecar.builtin.md", "MD", "MD", 0x40,
     {"CTL1","CTL2","CTL3","CTL4","CTL5","CTL6","CTL7","CTL8","MODL"},
     {64,64,64,64,64,64,64,64,0,0,100}}
};
inline const Synth* get(int handle) {
    return handle >= First && handle < First + Count ? &synths[handle-First] : nullptr;
}
inline int find(const char* id) {
    if (id) for (int i=0;i<Count;++i) if (!strcmp(id,synths[i].id)) return First+i;
    return -1;
}
inline void text(char* dst, unsigned size, const char* src) {
    memset(dst,0,size); if (src && size) strncpy(dst,src,size-1);
}
inline int names(int handle, unsigned p, char out[][8]) {
    static const char* const plaits[] = {"VA VCF","PHSDIST","WAVETRR","STRMACH","CHIPTUN","VA","WAVESHP","2-OP FM","GRANULR","HARMON","WAVETBL","CHORDS","SPEECH","SWARM","PARTICL","STRING","MODAL"};
    static const char* const daisy[] = {"MODAL","STRING","RESON"};
    static const char* const md[] = {"FM BD","FM SD","FM XT","FM CP","FM RS","FM CB","FM HH","FM CY","PI BD","PI SD","PI MT","PI ML","PI MA","PI RS","PI RC","PI CC","PI HH","TRX BD","TRX SD","TRX XT","TRX CP","TRX RS","TRX CB","TRX CH","TRX OH","TRX CY","TRX MA","TRX CL","TRX XC","TRX B2"};
    const char* const* list = nullptr; int n=0;
    if(handle==First && p==1) {list=plaits;n=17;}
    if(handle==First+2 && p==1) {list=daisy;n=3;}
    if(handle==First+4 && p==9) {list=md;n=30;}
    if(out) for(int i=0;i<n;++i) text(out[i],8,list[i]);
    return n;
}
inline void descriptor(int handle, um::NativeDescriptor& out) {
    out={}; const Synth* s=get(handle); if(!s)return;
    auto& d=out.desc; text(d.name,sizeof d.name,s->name); text(d.short_name,sizeof d.short_name,s->short_name);
    text(d.p2_title,sizeof d.p2_title,"SYNTH MORE"); d.page2=1;d.nknobs=16;d.pre=-1;d.format=4;
    for(unsigned i=0;i<16;++i) {
        auto& k=d.knob[i]; k.p=uint8_t(i+1); k.def=s->defaults[i];
        if(!s->labels[i] || !*s->labels[i]) {k.def=0;continue;}
        text(k.long_name,sizeof k.long_name,s->labels[i]);text(k.short_name,sizeof k.short_name,s->labels[i]);
        k.max=127; k.enum_count=uint8_t(names(handle,i+1,nullptr));
        k.look=k.enum_count?RMP_LOOK_LIST:RMP_LOOK_DIAL;
        k.kind=k.enum_count?RMP_TEXT_ENUM:RMP_TEXT_NUMBER;
    }
    out.state=RMP_PST_READY;out.flags=RMP_FLAG_PARAMS;
    out.revision=rmp_meta_print(out.state,out.flags,s->id,0x10000,&d);
}
inline void controls(int handle,const float* in,float* out) {
    const Synth* s=get(handle);
    for(unsigned i=0;i<16;++i) out[i]=s->labels[i]&&*s->labels[i]?in[i]:s->defaults[i];
    if(handle==First+4) {
        const int set=int(in[8])*30/128;
        const int engine=set<8?0:set<17?1:2, base=engine==0?0:engine==1?8:17, count=engine==0?8:engine==1?9:13;
        out[8]=float((engine*128+2)/3);out[9]=float(((set-base)*128+count-1)/count);out[10]=100;
    }
}
// Append to the existing catalog; the public protocol has 32 rows. The host
// reserves five for built-ins and admits the first 27 installed plugin entries.
template<class Plugins> void catalog(Plugins& plugins,unsigned index,um::NativeCatalogEntry& out) {
    um::NativeCatalogEntry p; plugins.Catalog(0,p);
    unsigned n=p.count>32-Count?32-Count:p.count;
    out={}; out.count=uint8_t(n+Count);out.revision=p.revision^0x42495331u;
    if(index<n) {plugins.Catalog(index,out);out.count=uint8_t(n+Count);out.revision=p.revision^0x42495331u;}
    else if(index<n+Count) {
        const Synth& s=synths[index-n];text(out.id,sizeof out.id,s.id);text(out.name,sizeof out.name,s.name);
        text(out.short_name,sizeof out.short_name,s.short_name);out.state=RMP_PST_READY;
    }
}
} } // namespace dn2_native::builtin
