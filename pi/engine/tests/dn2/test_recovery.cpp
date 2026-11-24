// Consume packets from actual DN2 SAVE/LOAD + capture, through the real session
// and part receivers. Sink models readiness/slot churn, not DSP or filesystem.
#include "dn2_session.h"
#include "dn2_parts.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using namespace dn2_native;
using Packet = std::array<uint8_t, GenericPacketBytes>;
struct Sink {
    bool installed[8]{true,true,true,true,true,true,true,false};
    bool reordered=false;
    struct Part { std::string id; float params[16]{}; int slot=-1; unsigned ons=0,sets=0; } part[8];
    int Ready(const char* id) {
        for (int i=0;i<8;++i) {
            char name[32];std::snprintf(name,sizeof name,"recall.engine%d",i);
            if (!std::strcmp(id,name)) return installed[i]?(reordered?7-i:i):-1;
        }
        return -1;
    }
    uint32_t Epoch(int) { return reordered?2:1; }
    void Request(unsigned p,const char* id) { part[p].id=id; }
    void Release(unsigned p) { part[p].id.clear();part[p].slot=-1; }
    void Kill(unsigned) {}
    void Off(unsigned) {}
    void Params(unsigned p,int slot,const float* values,float level) {
        CHECK(level==1);part[p].slot=slot;++part[p].sets;
        std::memcpy(part[p].params,values,sizeof part[p].params);
    }
    void On(unsigned p,float note,float velocity) {
        CHECK(note==60 && velocity==100.f/127.f);++part[p].ons;
    }
};
struct Endpoint {
    Sink sink;
    Parts<Sink> parts{sink};
    bool Send(LinkControl,uint32_t) { return true; }
    bool Begin(unsigned track,uint8_t returns) { CHECK(returns==1);return parts.Begin(track); }
    void End() { parts.End(); }
    bool Event(const uint8_t* p,size_t n) { return parts.Receive(p,n); }
};
int main(int argc,char** argv) {
    CHECK(argc==2);
    std::array<Packet,16> packets{};
    FILE* f=std::fopen(argv[1],"rb");CHECK(f);
    CHECK(std::fread(packets.data(),sizeof(Packet),packets.size(),f)==16);
    CHECK(std::fgetc(f)==EOF);std::fclose(f);
    for(unsigned i=0;i<16;++i) {
        Event e{};CHECK(decode(packets[i].data(),98,e));
        CHECK(e.sequence==i && e.track<8 && e.voice==15-e.track);
        CHECK(e.flags==(i<8?State:State|On));
        char id[32];std::snprintf(id,sizeof id,"recall.engine%u",e.track);CHECK(!std::strcmp(id,e.engine));
        for(unsigned k=0;k<16;++k)CHECK(e.params[k]==(e.track*1777+k*1231+129)%32513);
    }
    Endpoint ep;Session<Endpoint> session(ep);
    CHECK(session.Configure(kAllTracks,123,1));
    auto begin=[&](unsigned now,unsigned usb_epoch) {
        session.Poll(now,true,usb_epoch);
        std::array<uint8_t,50> ack{};
        CHECK(encode_link({LinkCommand::Ack,session.Token(),kAllTracks,1},ack.data(),ack.size()));
        CHECK(session.Receive(ack.data(),ack.size(),now+1));
        // Pre-reset data cannot restore a previous session's sound.
        CHECK(!session.Receive(packets[0].data(),98,now+2));
        Event reset{};reset.sequence=65535;reset.flags=Reset;
        Packet p{};CHECK(encode_generic(reset,p.data(),p.size()));
        CHECK(session.Receive(p.data(),p.size(),now+3));
    };
    auto controls=[&](unsigned p) {
        CHECK(ep.sink.part[p].slot==(ep.sink.reordered?7-int(p):int(p)));
        for(unsigned k=0;k<16;++k)CHECK(ep.sink.part[p].params[k]==((p*1777+k*1231+129)%32513)/256.f);
    };
    begin(0,1);
    for(unsigned i=0;i<8;++i)CHECK(session.Receive(packets[i].data(),98,10+i));
    for(unsigned p=0;p<8;++p) {
        CHECK(ep.sink.part[p].ons==0);
        CHECK(ep.sink.part[p].id=="recall.engine"+std::to_string(p));
        if(p<7) controls(p);else CHECK(ep.sink.part[p].slot==-1);
    }
    // Reinstallation while stopped restores all controls without needing a knob
    // movement and without replaying an old note. Other parts remain untouched.
    ep.sink.installed[7]=true;ep.parts.Poll();
    for(unsigned p=0;p<8;++p) { controls(p);CHECK(ep.sink.part[p].sets==1 && ep.sink.part[p].ons==0); }
    for(unsigned i=8;i<16;++i)CHECK(session.Receive(packets[i].data(),98,30+i));
    for(unsigned p=0;p<8;++p)CHECK(ep.sink.part[p].ons==1);
    // Duplicate packets must not replay a saved hit.
    CHECK(!session.Receive(packets[15].data(),98,50));
    const auto old_token=session.Token();session.Disconnect();
    for(auto& p:ep.sink.part)CHECK(p.id.empty() && p.slot==-1);
    ep.sink.reordered=true;ep.sink.installed[7]=false;
    begin(100,2);CHECK(session.Token()!=old_token);
    std::array<uint8_t,50> stale{};
    CHECK(encode_link({LinkCommand::Ack,old_token,kAllTracks,1},stale.data(),stale.size()));
    CHECK(!session.Receive(stale.data(),stale.size(),104));
    for(unsigned i=0;i<16;++i)CHECK(session.Receive(packets[i].data(),98,110+i));
    CHECK(ep.sink.part[7].ons==1); // the unavailable hit was not played
    ep.sink.installed[7]=true;ep.parts.Poll();
    for(unsigned p=0;p<8;++p) { controls(p);CHECK(ep.sink.part[p].ons==(p==7?1u:2u)); }
    // Cancel a missing assignment before it finishes loading: it cannot return.
    ep.sink.installed[7]=false;
    Event e{};CHECK(decode(packets[8].data(),98,e));CHECK(e.track==7);
    e.sequence=16;Packet next{};CHECK(encode_generic(e,next.data(),98));
    CHECK(session.Receive(next.data(),98,140));
    e.sequence=17;e.flags=Kill;CHECK(encode_generic(e,next.data(),98));
    CHECK(session.Receive(next.data(),98,141));
    ep.sink.installed[7]=true;ep.parts.Poll();CHECK(ep.sink.part[7].id.empty() && ep.sink.part[7].slot==-1);
    session.Disconnect();ep.parts.Poll();
    for(auto& p:ep.sink.part)CHECK(p.id.empty());
    std::puts("saved eight-part recovery: exact controls, missing/reinstalled engine, no late hits, reordered slots, reconnect, stale ACK/data, cancelled load passed");
}
