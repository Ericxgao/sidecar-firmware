#include "dn2_native.h"
#include "dn2_parts.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace dn2_native;
using Packet=std::array<uint8_t,GenericPacketBytes>;
static Event event(unsigned seq,unsigned part,unsigned voice,const char* id,unsigned flags=State|On){
    Event e{};e.sequence=uint16_t(seq);e.tick=0x89abcdef;e.track=uint8_t(part);e.voice=uint8_t(voice);e.flags=uint8_t(flags);
    e.note=0x3c80;e.velocity=0x6481;e.level=0x7f00;
    for(unsigned i=0;i<16;++i)e.params[i]=uint16_t(i*2001+171);
    CHECK(std::strlen(id)<32);std::strcpy(e.engine,id);return e;
}
// Construct the documented binary fields independently of encode_generic().
static Packet fixture(){
    std::array<uint8_t,79> raw{0xff,0xfe,0x89,0xab,0xcd,0xef,7,15,3,0x3c,0x80,0x64,0x81,0x7f,0};
    for(unsigned i=0;i<16;++i){unsigned v=i*2001+171;raw[15+2*i]=uint8_t(v>>8);raw[16+2*i]=uint8_t(v);}
    std::memcpy(raw.data()+47,"demo.bell",10);
    Packet p{0xf0,0x7d,0x44,0x32,2,1};unsigned at=6;
    for(unsigned i=0;i<raw.size();){unsigned mask=at++;for(unsigned b=0;b<7&&i<raw.size();++i,++b){p[mask]|=uint8_t((raw[i]>>7)<<b);p[at++]=raw[i]&127;}}
    CHECK(at==97);p[at]=0xf7;return p;
}
struct Sink {
    int order=0; bool fixture_ready=true;
    uint32_t epochs[8]{1,1,1,1,1,1,1,1};
    struct Part { std::string calls,requested; unsigned releases=0; int slot=-1;float p[16]{}; } parts[8];
    int Ready(const char* id){
        if(!std::strncmp(id,"test.engine",11) && id[11]>='0' && id[11]<='7' && !id[12])return id[11]-'0';
        if(!std::strcmp(id,"mega.perkons"))return order?3:0;
        if(!std::strcmp(id,"demo.bell"))return fixture_ready?(order?0:3):-1;
        if(!std::strcmp(id,"future.engine"))return 4;
        return -1;
    }
    uint32_t Epoch(int slot){return epochs[slot];}
    void Params(unsigned p,int slot,const float* values,float){parts[p].calls+='P';parts[p].slot=slot;std::memcpy(parts[p].p,values,sizeof parts[p].p);}
    void On(unsigned p,float,float){parts[p].calls+='N';}
    void Off(unsigned p){parts[p].calls+='O';}
    void Kill(unsigned p){parts[p].calls+='K';}
    void Request(unsigned p,const char* id){parts[p].requested=id;}
    void Release(unsigned p){Kill(p);parts[p].requested.clear();++parts[p].releases;}
    void clear(){for(auto& p:parts)p.calls.clear();}
};
static void deferred_loading(){
    Sink sink; sink.fixture_ready=false; Parts<Sink> bank(sink); CHECK(bank.Begin(kAllTracks));
    unsigned seq=0;
    auto send=[&](unsigned part,unsigned voice,const char* id,unsigned flags,unsigned value=0x4501){
        auto e=event(seq++,part,voice,id,flags);e.params[15]=uint16_t(value);
        Packet p{};CHECK(encode_generic(e,p.data(),p.size()));CHECK(bank.Receive(p.data(),p.size()));
    };
    sink.clear();send(0,0,"mega.perkons",State|On);send(1,14,"demo.bell",State|On);
    CHECK(sink.parts[0].calls=="KPN"&&sink.parts[1].calls=="K");
    CHECK(sink.parts[1].requested=="demo.bell"&&bank.Part(1).Owner()==14);
    const auto releases=sink.parts[1].releases;
    sink.clear();bank.Poll();CHECK(sink.parts[1].calls.empty());
    send(1,14,"demo.bell",State,0x5a81);CHECK(sink.parts[1].calls=="K");
    sink.clear();send(1,2,"mega.perkons",State|Off);send(1,2,"demo.bell",Kill);
    CHECK(sink.parts[1].calls.empty()&&sink.parts[1].requested=="demo.bell");
    sink.fixture_ready=true;bank.Poll();
    CHECK(sink.parts[1].calls=="P"&&sink.parts[1].p[15]==0x5a81/256.f);
    CHECK(sink.parts[0].calls.empty()&&sink.parts[1].releases==releases);
    sink.clear();bank.Poll();CHECK(sink.parts[1].calls.empty());
    send(1,14,"demo.bell",State|On);CHECK(sink.parts[1].calls=="PN"); // only a new hit plays
    sink.clear();send(1,14,"demo.bell",State|Kill);
    CHECK(sink.parts[1].calls=="PK"&&sink.parts[1].requested=="demo.bell"&&sink.parts[1].releases==releases);
    sink.clear();send(0,0,"",Reset);
    CHECK(sink.parts[1].calls=="K"&&sink.parts[1].requested=="demo.bell");
    // A newer committed engine replaces pending intent, and a same-voice stale
    // removal edge naming the old engine cannot release the new assignment.
    sink.fixture_ready=false;sink.clear();send(1,14,"demo.bell",State|On);
    send(1,14,"mega.perkons",State);CHECK(sink.parts[1].requested=="mega.perkons");
    sink.clear();sink.fixture_ready=true;bank.Poll();send(1,14,"demo.bell",Kill);
    CHECK(sink.parts[1].calls.empty());
    send(1,14,"mega.perkons",Kill);CHECK(sink.parts[1].calls=="K"&&sink.parts[1].requested.empty());
    // Stop cancels the pending control restore; loading may finish in cache,
    // but neither old controls nor old note-ons are replayed after stop/end.
    sink.fixture_ready=false;send(1,14,"demo.bell",State|On);send(0,0,"",Reset);
    sink.clear();sink.fixture_ready=true;bank.Poll();CHECK(sink.parts[1].calls.empty());
    sink.fixture_ready=false;send(1,14,"demo.bell",State|On);bank.End();
    sink.clear();sink.fixture_ready=true;bank.Poll();
    for(auto& p:sink.parts)CHECK(p.calls.empty()&&p.requested.empty());
    std::puts("deferred native loading: latest controls without late hits, pending-owner isolation, replacement, mute/stop reservations and teardown passed");
}
int main(int argc,char** argv){
    if(argc==2){
        Packet wire{},again{};Event e{};
        FILE* f=std::fopen(argv[1],"rb");CHECK(f);
        CHECK(std::fread(wire.data(),1,wire.size(),f)==wire.size());CHECK(std::fgetc(f)==EOF);std::fclose(f);
        CHECK(decode(wire.data(),wire.size(),e));
        CHECK(e.tick==0x89abcdef&&e.sequence==0xfffe&&e.track==14&&e.voice==15&&e.flags==7);
        CHECK(e.note==0x3c80&&e.velocity==0x6481&&e.level==0x7f00&&!std::strcmp(e.engine,"demo.bell"));
        for(unsigned i=0;i<16;++i)CHECK(e.params[i]==uint16_t(0x81ffu*(i+1)));
        CHECK(encode_generic(e,again.data(),again.size())&&again==wire);
        std::puts("compiled ColdFire v2 packet: full ID and sixteen words decoded and re-encoded byte-exact");
    }
    deferred_loading();
    const Packet expected=fixture();Packet encoded{};
    const auto original=event(65534,7,15,"demo.bell");
    CHECK(encode_generic(original,encoded.data(),encoded.size())&&encoded==expected);
    Event decoded{};CHECK(decode(expected.data(),expected.size(),decoded));
    CHECK(decoded.tick==original.tick&&decoded.sequence==original.sequence&&decoded.note==original.note&&decoded.velocity==original.velocity&&decoded.level==original.level);
    CHECK(!std::strcmp(decoded.engine,"demo.bell")&&decoded.track==7&&decoded.voice==15);
    for(unsigned i=0;i<16;++i)CHECK(decoded.params[i]==original.params[i]);
    auto reject=[&](const uint8_t* p,size_t n){Event e=original;CHECK(!decode(p,n,e));CHECK(!std::memcmp(&e,&original,sizeof e));};
    reject(nullptr,98);
    for(size_t n=0;n<120;++n)if(n!=98)reject(expected.data(),n);
    for(unsigned i=1;i<97;++i){auto p=expected;p[i]|=128;reject(p.data(),p.size());}
    for(unsigned i:{0u,1u,2u,3u,4u,5u,97u}){auto p=expected;p[i]^=1;reject(p.data(),p.size());}
    {auto p=expected;p[94]|=4;reject(p.data(),p.size());} // final mask has two valid bits
    for(const char* bad:{"",".bad","bad..id","bad/id","bad\\id","bad id"}){
        auto e=event(1,0,0,bad);auto p=expected;CHECK(!encode_generic(e,p.data(),p.size())&&p==expected);
    }
    {auto e=original;e.engine[20]='x';CHECK(!encode_generic(e,encoded.data(),encoded.size()));}
    {auto e=original;std::memset(e.engine,'a',32);CHECK(!encode_generic(e,encoded.data(),encoded.size()));}
    for(size_t n=0;n<98;++n){auto p=expected;CHECK(!encode_generic(original,p.data(),n)&&p==expected);}
    auto longest=event(1,0,0,"abcdefghijklmnopqrstuvwxyz12345");CHECK(encode_generic(longest,encoded.data(),98));CHECK(decode(encoded.data(),98,decoded));CHECK(!std::strcmp(longest.engine,decoded.engine));
    Sink sink;Parts<Sink> bank(sink);CHECK(bank.Begin(kAllTracks));unsigned seq=65530;
    auto send=[&](unsigned part,unsigned voice,const char* id,unsigned flags=State|On){auto e=event(seq++,part,voice,id,flags);Packet p{};CHECK(encode_generic(e,p.data(),98));return bank.Receive(p.data(),98);};
    CHECK(send(0,0,"",Reset));sink.clear();
    for(unsigned p=0;p<8;++p)CHECK(send(p,15-p,p%2?"demo.bell":"mega.perkons"));
    for(unsigned p=0;p<8;++p){CHECK(sink.parts[p].calls=="KPN");CHECK(sink.parts[p].slot==(p%2?3:0));CHECK(sink.parts[p].p[15]==(15*2001+171)/256.f);}
    sink.clear(); ++sink.epochs[4]; // an unrelated newly loaded image must not cut existing tails
    CHECK(send(0,15,"mega.perkons",State) && send(1,14,"demo.bell",State));
    CHECK(sink.parts[0].calls=="P" && sink.parts[1].calls=="P");
    sink.clear(); ++sink.epochs[3]; // a replacement of this engine retires only its own part
    CHECK(send(0,15,"mega.perkons",State) && send(1,14,"demo.bell",State));
    CHECK(sink.parts[0].calls=="P" && sink.parts[1].calls=="KP");
    sink.clear();CHECK(send(2,13,"future.engine"));CHECK(sink.parts[2].calls=="KPN"&&sink.parts[2].slot==4);
    sink.clear();CHECK(send(2,3,"demo.bell"));CHECK(sink.parts[2].calls=="KPN");
    sink.clear();CHECK(send(2,13,"future.engine",Kill));CHECK(sink.parts[2].calls.empty()); // old owner cannot switch back
    CHECK(send(2,3,"missing.engine",State));CHECK(sink.parts[2].calls=="K"&&bank.Part(2).unavailable==1);
    sink.clear();sink.order=1;CHECK(send(0,15,"mega.perkons",State));CHECK(sink.parts[0].slot==3&&sink.parts[0].calls=="KP");
    for(unsigned p=1;p<8;++p)CHECK(sink.parts[p].calls.empty());
    CHECK(send(0,15,"mega.perkons",State|Off));CHECK(sink.parts[0].calls=="KPPO");
    sink.clear();CHECK(send(0,15,"mega.perkons",State|On|Kill));CHECK(sink.parts[0].calls=="PK");
    // Eight different IDs mapped to eight resident slots, then a ninth is unavailable.
    for (unsigned p=0;p<8;++p) {
        char id[]="test.engine0"; id[11]=char('0'+p);
        sink.clear();CHECK(send(p,15-p,id));
        CHECK(sink.parts[p].slot==int(p) && sink.parts[p].calls=="KPN");
        for(unsigned other=0;other<8;++other)if(other!=p)CHECK(sink.parts[other].calls.empty());
    }
    std::puts("generic native: sixteen fractional controls, full persistent IDs, mixed eight parts, slot reassignment, missing engines and stale-owner rejection passed");
}
