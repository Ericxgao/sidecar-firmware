#include "dn2_builtins.h"
#include "dn2_parts.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace dn2_native;
struct Plugins {
    unsigned count=17;
    void Catalog(unsigned index,um::NativeCatalogEntry& e) {
        e={};e.count=uint8_t(count);e.revision=123;
        if(index<count) {std::snprintf(e.id,sizeof e.id,"test.plugin%u",index);e.state=RMP_PST_EMPTY;}
    }
};
struct Sink {
    int handle[8]{},ons[8]{},offs[8]{},releases[8]{};float value[8]{};
    int Ready(const char* id) {int h=builtin::find(id);return h>=0?h:!strcmp(id,"test.plugin")?7:-1;}
    uint32_t Epoch(int) {return 1;}
    void Params(unsigned p,int h,const float* v,float) {handle[p]=h;value[p]=v[1];}
    void On(unsigned p,float,float) {++ons[p];}
    void Off(unsigned p) {++offs[p];}
    void Kill(unsigned) {}
    void Request(unsigned,const char*) {}
    void Release(unsigned p) {++releases[p];}
};
int main() {
    Plugins p;um::NativeCatalogEntry e;
    for(unsigned i=0;i<22;++i) {
        builtin::catalog(p,i,e);assert(e.count==22 && engine_id_ok(e.id));
        if(i<17) assert(std::string(e.id)=="test.plugin"+std::to_string(i));
        else assert(builtin::find(e.id)==int(i-17)+builtin::First);
    }
    builtin::catalog(p,22,e);assert(e.count==22 && !e.id[0]);
    p.count=32;builtin::catalog(p,31,e);assert(e.count==32 && builtin::find(e.id)==builtin::First+4);
    assert(builtin::find("sidecar.builtin.unknown")==-1);
    for(int h=builtin::First;h<builtin::First+builtin::Count;++h) {
        um::NativeDescriptor d;builtin::descriptor(h,d);assert(d.state==RMP_PST_READY && d.desc.nknobs==16);
        for(unsigned k=0;k<16;++k) {
            const auto& q=d.desc.knob[k];assert(q.def>=q.min && q.def<=q.max);
            char names[36][8]{};assert(builtin::names(h,k+1,names)==q.enum_count);
            for(unsigned n=0;n<q.enum_count;++n) assert(names[n][0] && names[n][7]==0);
        }
    }
    for(int model=0;model<30;++model) {
        float in[16]{},out[16]{};in[8]=float((model*128+29)/30);builtin::controls(builtin::First+4,in,out);
        int engine=model<8?0:model<17?1:2,count=engine==0?8:engine==1?9:13,base=engine==0?0:engine==1?8:17;
        assert(int(out[8])*3/128==engine && int(out[9])*count/128==model-base && out[10]==100);
    }
    Sink s;Parts<Sink> parts(s);assert(parts.Begin(16));Event a{};a.flags=State|On;a.note=60*256;a.velocity=a.level=32512;
    uint8_t bytes[GenericPacketBytes];
    auto send=[&]() {++a.sequence;assert(encode_generic(a,bytes,sizeof bytes));assert(parts.Receive(bytes,sizeof bytes));};
    for(unsigned track=0;track<8;++track) {
        a.track=uint8_t(track);a.voice=uint8_t(track+8);a.params[1]=uint16_t(64*256+track);
        builtin::text(a.engine,sizeof a.engine,builtin::synths[track%5].id);send();
        assert(s.handle[track]==builtin::First+int(track%5) && s.ons[track]==1);
    }
    a.track=2;a.voice=10;a.flags=State|Off;builtin::text(a.engine,sizeof a.engine,builtin::synths[2].id);send();
    assert(s.offs[2]==1 && s.offs[1]==0);
    a.flags=State|On;builtin::text(a.engine,sizeof a.engine,"test.plugin");send();assert(s.handle[2]==7 && s.ons[2]==2);
    builtin::text(a.engine,sizeof a.engine,"unknown.engine");send();parts.Poll();assert(s.ons[2]==2);
    assert(s.value[1]==64.0f+1.0f/256);
    puts("PASS: unified catalog, bounds, metadata, all MD model mappings, eight built-in handles, plugin transitions and fractional controls");
}
