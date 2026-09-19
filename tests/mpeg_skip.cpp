#include "core/mpeg/audio.h"
#include "core/mpeg/demux.h"
#include "core/mpeg/video.h"
#include "core/mpeg/player.h"
#include <cassert>
#include <cstdio>
#include <utility>
#include <algorithm>
using namespace n32;
int main(int argc, char** argv) {
    mpeg::PlaybackBudget budget;
    assert(!budget.reduceWork());
    budget.observe(60000);
    assert(budget.reduceWork());
    budget.observe(1000);
    assert(budget.reduceWork()); // One cheap tick must not erase a heavy frame.
    budget.observe(1000);
    assert(!budget.reduceWork());
    budget.observe(~0u); // Saturation and bounded recovery, no unsigned wrap.
    for (int i = 0; i < 4; ++i) { budget.observe(0); assert(budget.reduceWork()); }
    budget.observe(0); assert(!budget.reduceWork());
    for (int i = 0; i < 100; ++i) budget.observe(22000);
    assert(!budget.reduceWork());
    assert(argc == 2);
    FILE* f = std::fopen(argv[1], "rb"); assert(f);
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); assert(n > 0);
    std::rewind(f); std::vector<u8> bytes(n);
    assert(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size()); std::fclose(f);
    {
        auto inaccurate=bytes;bool patched=false;
        for(size_t i=0;i+10<inaccurate.size();++i) {
            if(inaccurate[i]==0 && inaccurate[i+1]==0 && inaccurate[i+2]==1 && inaccurate[i+3]==0xbb) {
                inaccurate[i+9]&=3;patched=true;break;
            }
        }
        assert(patched);
        const char* path="tests/out/zero-audio-bound.mpg";
        FILE* out=fopen(path,"wb");assert(out);
        assert(fwrite(inaccurate.data(),1,inaccurate.size(),out)==inaccurate.size());fclose(out);
        auto source=mpeg::openProgramStream(path,mpeg::PACKET_AUDIO_1);assert(source);
        auto reference=mpeg::demuxAll(bytes);
        mpeg::Audio streamed{mpeg::Buffer(source)}, baseline(reference.audio);
        assert(streamed.hasHeader() && streamed.sampleRate()==baseline.sampleRate());
        mpeg::Samples x,y;unsigned audible=0;
        for(;;) {
            bool more=baseline.decode(&x);assert(streamed.decode(&y)==more);if(!more)break;
            assert(x.interleaved==y.interleaved);
            for(float sample:y.interleaved)if(sample!=0){++audible;break;}
        }
        assert(audible>20);remove(path);
        puts("PASS: declared zero audio streams still discovers C0 packets and decodes identical audible PCM");
    }
    mpeg::Demux copied(bytes),viewed(bytes,false);mpeg::Packet aPacket,bPacket;
    std::vector<u8> expectedVideo,expectedAudio;
    while(copied.decode(&aPacket)) {
        assert(viewed.decode(&bPacket) && bPacket.data.empty());
        assert(aPacket.type==bPacket.type && aPacket.pts==bPacket.pts && aPacket.data.size()==bPacket.payloadSize);
        assert(std::vector<u8>(bPacket.payload,bPacket.payload+bPacket.payloadSize)==aPacket.data);
        if(aPacket.type==mpeg::PACKET_VIDEO_1)expectedVideo.insert(expectedVideo.end(),aPacket.data.begin(),aPacket.data.end());
        if(aPacket.type==mpeg::PACKET_AUDIO_1)expectedAudio.insert(expectedAudio.end(),aPacket.data.begin(),aPacket.data.end());
    }
    assert(!viewed.decode(&bPacket));
    mpeg::DemuxedStreams streams = mpeg::demuxAll(std::move(bytes));
    assert(streams.video==expectedVideo && streams.audio==expectedAudio);
    std::puts("PASS: allocation-free demux packet views and two-pass output match copied packets byte-for-byte");
    {
        auto videoSource=mpeg::openProgramStream(argv[1],mpeg::PACKET_VIDEO_1);
        auto audioSource=mpeg::openProgramStream(argv[1],mpeg::PACKET_AUDIO_1);
        assert(videoSource && audioSource);
        mpeg::Video streamed{mpeg::Buffer(videoSource)}, baseline(streams.video);
        size_t a=0,b=0,count=0;
        for(;;) {
            bool more=baseline.decode(&a);assert(streamed.decode(&b)==more);if(!more)break;
            const auto* x=baseline.frame(a);const auto* y=streamed.frame(b);
            assert(x->y.data==y->y.data && x->cr.data==y->cr.data && x->cb.data==y->cb.data);++count;
        }
        assert(count>20);
        mpeg::Audio sa{mpeg::Buffer(audioSource)}, ba(streams.audio);mpeg::Samples x,y;
        unsigned frames=0;
        for(;;){bool more=ba.decode(&x);assert(sa.decode(&y)==more);if(!more)break;assert(x.interleaved==y.interleaved);++frames;}
        assert(frames>20);
        std::puts("PASS: file-streamed video planes and MP2 samples equal whole-file baseline");
    }
    {
        const char* path="tests/out/large-stream.mpg";
        FILE* output=fopen(path,"wb");FILE* input=fopen(argv[1],"rb");assert(output && input);
        unsigned char chunk[16384];
        for(unsigned repeat=0;repeat<64;++repeat) {
            rewind(input);size_t got;
            while((got=fread(chunk,1,sizeof(chunk),input)))assert(fwrite(chunk,1,got,output)==got);
        }
        fclose(input);fclose(output);
        for(int type:{mpeg::PACKET_VIDEO_1,mpeg::PACKET_AUDIO_1}) {
            const auto& expected=type==mpeg::PACKET_VIDEO_1?streams.video:streams.audio;
            auto source=mpeg::openProgramStream(path,type);assert(source);
            mpeg::Buffer bounded(source);size_t total=0,peak=0;
            while(bounded.has(8)) {
                size_t available=std::min((size_t)4096,bounded.data.size()-(bounded.bitIndex>>3));
                for(size_t i=0;i<available;++i)assert(bounded.data[(bounded.bitIndex>>3)+i]==expected[(total+i)%expected.size()]);
                total+=available;bounded.skip(available*8);bounded.discardReadBytes();
                peak=std::max(peak,bounded.data.capacity());assert(peak<=mpeg::Buffer::streamLimit);
            }
            assert(total==expected.size()*64);
            printf("PASS: large streamed type=%d bytes=%zu ES peak capacity=%zu\n",type,total,peak);
        }
        remove(path);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        mpeg::Video reference(streams.video), reduced(streams.video);
        size_t count = 0, drops = 0, a = 0, b = 0;
        for (;;) {
            bool skipped = false;
            bool more = reference.decode(&a);
            assert(reduced.decode(&b, mode == 0 || count % (mode + 1) == 0, &skipped) == more);
            if (!more) break;
            ++count;
            assert(reference.lastFrameTime() == reduced.lastFrameTime());
            if (skipped) { ++drops; continue; }
            const mpeg::Frame* x = reference.frame(a); const mpeg::Frame* y = reduced.frame(b);
            assert(x->y.data == y->y.data && x->cr.data == y->cr.data && x->cb.data == y->cb.data);
        }
        assert(count > 20 && drops < count);
        // A periodic request may hit only I/P slots; those must never drop.
        if (mode == 0) assert(drops > 0);
        std::printf("PASS skip mode=%u frames=%zu skipped=%zu retained planes/time identical\n", mode, count, drops);
    }
    mpeg::VideoPlayer full(streams.video), reduced(streams.video);
    std::vector<u32> a, b;
    unsigned ticks = 0;
    while (!full.isFinished()) {
        full.advanceAndRender(1.0 / 30, &a, 320, 240);
        reduced.advanceAndRender(1.0 / 30, &b, 320, 240, true);
        assert(full.isFinished() == reduced.isFinished());
        assert(full.elapsed() == reduced.elapsed());
        assert(++ticks < 10000);
    }
    assert(reduced.skippedFrames() > 0 && b.size() == 320 * 240);
    // Repeated display, new destination and resizing must still produce pixels.
    std::vector<u32> other;
    reduced.advanceAndRender(0, &other, 320, 240);
    assert(other == b);
    reduced.advanceAndRender(0, &other, 160, 120);
    assert(other.size() == 160 * 120);
    std::puts("PASS: reduced playback preserves duration/end tick and retained-buffer resize");
}
