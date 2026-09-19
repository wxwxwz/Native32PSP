#include "core/audio_engine.h"
#include "pspmp3.h"
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace n32;
namespace n32 { void pspLog(const char*,...) {} }
static int accepted,consumed,total,loop,rate=11025,channels=1,frames,failStage;
static bool resource,handle;
static unsigned char input[64*1024];
static short pcm[32];
static std::vector<float> reference;
int sceMp3InitResource() { resource=true; return 0; }
int sceMp3TermResource() { resource=false; return 0; }
int sceMp3ReserveMp3Handle(SceMp3InitArg* a) {
    assert(((uintptr_t)a->mp3Buf%64)==0 && ((uintptr_t)a->pcmBuf%64)==0);
    total=a->mp3StreamEnd; accepted=consumed=frames=0; loop=-1; handle=true; reference.clear(); return 1;
}
int sceMp3ReleaseMp3Handle(int) { handle=false; return 0; }
int sceMp3Init(int) { return failStage==1?-1:0; }
int sceMp3GetInfoToAddStreamData(int,unsigned char** p,int* n,int* offset) {
    *p=input; *n=sizeof(input); *offset=accepted; return 0;
}
int sceMp3NotifyAddStreamData(int,int n) { accepted+=n; return failStage==2?-2:0; }
// Intentionally requests more even at EOF, as buffering/low-water requests can.
int sceMp3CheckStreamDataNeeded(int) { return 1; }
int sceMp3SetLoopNum(int,int n) { loop=n; return 0; }
int sceMp3GetSamplingRate(int) { return rate; }
int sceMp3GetMp3ChannelNum(int) { return channels; }
int sceMp3Decode(int,short** out) {
    assert(loop==0);
    if(failStage==3) return -3;
    if(consumed==total) return 0;
    assert(accepted-consumed>=25); consumed+=25; ++frames;
    for(int i=0;i<8;++i) for(int c=0;c<channels;++c) {
        pcm[i*channels+c]=(short)((frames*17+i*3+c*5)%20000-10000);
        reference.push_back((float)pcm[i*channels+c]/32768.0f);
    }
    *out=pcm; return 8*channels*2;
}
int main() {
    for(int length : {100,100000}) for(int sampleRate : {11025,22050,44100,48000}) for(int ch : {1,2}) {
        rate=sampleRate; channels=ch; failStage=0;
        AudioEngine engine(ColorspaceYuv,100);
        size_t id=engine.playMp3(std::vector<u8>(length,0xaa),0,"music");
        assert(id && frames==length/25 && !resource && !handle);
        std::vector<s16> expected=resampleToStereo(reference,ch,rate,11025), got;
        while(got.size()<expected.size()) {
            auto block=engine.getPendingSamples(); got.insert(got.end(),block.begin(),block.end());
        }
        for(size_t i=0;i<expected.size();++i) assert(std::abs((int)expected[i]-(int)got[i])<=2);
    }
    for(failStage=1;failStage<=3;++failStage) {
        AudioEngine engine;
        assert(!engine.playMp3(std::vector<u8>(100,0),0,"music") && !resource && !handle);
    }
    FILE* file=fopen("tests/fixtures/mp3-mpeg2-mono.mp3","rb");assert(file);
    fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);
    std::vector<u8> music(length);assert(fread(music.data(),1,music.size(),file)==music.size());fclose(file);
    failStage=1;AudioEngine fallback(ColorspaceYuv,100);
    assert(fallback.playMp3(music,0,"fallback") && !resource && !handle);
    size_t audible=0;for(int i=0;i<60;++i)for(s16 v:fallback.getPendingSamples())if(v)++audible;
    assert(audible>10000 && !fallback.isPlaying() && fallback.retainedAudioBytes()==0);
    // Loop and repeated stop/restart must neither retain decoded songs nor leak channels.
    auto expectedPcm=decodeSoftwareMp3(music,11025);
    for(int pass=0;pass<50;++pass) {
        assert(fallback.playMp3(music,1,"loop"));
        assert(fallback.retainedAudioBytes()<music.size()+30000);
        std::vector<s16> played;
        while(fallback.isPlaying()) {auto b=fallback.getPendingSamples();played.insert(played.end(),b.begin(),b.end());}
        assert(played.size()>=expectedPcm.size()*2);
        for(size_t i=0;i<expectedPcm.size()*2;++i)assert(played[i]==expectedPcm[i%expectedPcm.size()]);
        assert(fallback.retainedAudioBytes()==0);
        assert(fallback.playMp3(music,255,"infinite"));
        for(int tick=0;tick<100;++tick)fallback.getPendingSamples();
        fallback.stopForMovie("infinite");assert(fallback.retainedAudioBytes()==0);
        assert(fallback.playMp3(music,255,"stop"));fallback.stopAll();assert(fallback.retainedAudioBytes()==0);
    }
    std::puts("PASS: streaming PCM matches full decode across blocks/loops; 50 start/finish/stop cycles release all channel storage");
    std::puts("PASS: MPEG-2 22050 Hz mono music falls back after hardware init rejection and reaches the mixer");
    std::puts("PASS: MP3 buffered EOF drain, loop disable, mono/stereo rate conversion and error cleanup");
}
