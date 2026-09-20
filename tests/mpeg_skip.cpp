#include "core/mpeg/audio.h"
#include "core/mpeg/demux.h"
#include "core/mpeg/video.h"
#include "core/mpeg/player.h"
#include <cassert>
#include <cstdio>
#include <utility>
#include <algorithm>
using namespace n32;

// Independent pre-credit policy used only to reproduce periodic B starvation.
class ZeroCreditBudget {
public:
    ZeroCreditBudget() : debt(0) {}
    bool reduceWork() const { return debt != 0; }
    void observe(unsigned cost) {
        if(cost>22000)debt=std::min(100000u,debt+cost-22000);
        else debt=cost+debt>22000?cost+debt-22000:0;
    }
private:
    unsigned debt;
};
struct BudgetModelResult {
    unsigned references=0, bPictures=0, skipped=0, slots=0;
    unsigned long long videoMicros=0;
};
template<class Budget> static BudgetModelResult modelBudget(unsigned referenceMicros) {
    Budget budget;
    BudgetModelResult result;
    // Synthetic 25 FPS IBBP slots on a 30 Hz timeline, for 100 seconds.
    // These costs exercise scheduling; they are not measurements of a PSP.
    for(unsigned tick=0;tick<3000;++tick) {
        const unsigned target=(tick+1)*25/30;
        const bool reduce=budget.reduceWork();
        unsigned cost=0;
        while(result.slots<=target) {
            if(result.slots%3==0) {++result.references;cost+=referenceMicros+5000;}
            else if(reduce) {++result.skipped;cost+=1000;}
            else {++result.bPictures;cost+=14000+5000;}
            ++result.slots;
        }
        result.videoMicros+=cost;
        budget.observe(cost);
    }
    return result;
}
static void testWorkBudget() {
    mpeg::PlaybackBudget budget;
    assert(!budget.reduceWork());
    budget.observe(60000);
    assert(budget.reduceWork());
    budget.observe(1000);
    assert(budget.reduceWork()); // One cheap tick must not erase a heavy frame.
    budget.observe(1000);
    assert(!budget.reduceWork());
    budget.observe(~0u); // Saturation and bounded recovery, no signed/unsigned wrap.
    for(int i=0;i<4;++i) {budget.observe(0);assert(budget.reduceWork());}
    budget.observe(0);assert(!budget.reduceWork());
    for(int i=0;i<100;++i)budget.observe(22000);
    assert(!budget.reduceWork());

    // Even a long idle period can reserve only one 22 ms video budget.
    for(int i=0;i<1000;++i)budget.observe(0);
    budget.observe(44001);assert(budget.reduceWork());
    budget.observe(22000);assert(budget.reduceWork());
    budget.observe(21999);assert(!budget.reduceWork());
    for(int i=0;i<100;++i) {budget.observe(~0u);assert(budget.reduceWork());}
    for(int i=0;i<4;++i) {budget.observe(0);assert(budget.reduceWork());}
    budget.observe(0);assert(!budget.reduceWork());

    const auto old=modelBudget<ZeroCreditBudget>(60000);
    const auto current=modelBudget<mpeg::PlaybackBudget>(60000);
    assert(old.slots==2501 && old.references==834 && old.bPictures==0 && old.skipped==1667);
    assert(current.slots==old.slots && current.references==old.references);
    assert(current.bPictures==499 && current.skipped+current.bPictures==old.skipped);
    assert(current.videoMicros>old.videoMicros && current.videoMicros<=3000ull*22000);
    // When mandatory reference work already exceeds the budget, credit must
    // not turn B reconstruction back on or discard any reference/time slots.
    const auto overload=modelBudget<mpeg::PlaybackBudget>(80000);
    const auto oldOverload=modelBudget<ZeroCreditBudget>(80000);
    assert(overload.references==oldOverload.references && overload.slots==oldOverload.slots);
    assert(overload.bPictures==0 && overload.skipped==oldOverload.skipped);
    assert(overload.videoMicros==oldOverload.videoMicros);
    std::puts("PASS: bounded MPEG idle credit, overflow/recovery, periodic IBBP starvation and sustained-overload protection");
}
static unsigned pictureAttempts(const mpeg::DecodeDiagnostics& diagnostics) {
    unsigned count = 0;
    for (unsigned type = 0; type < 4; ++type) count += diagnostics.pictureAttempts[type];
    return count;
}
static void checkPictureDiagnostics(const std::vector<u8>& videoEs, unsigned trailingType) {
    // Read picture type directly from the elementary stream headers, without
    // relying on decoder bookkeeping or display-order frame returns.
    unsigned expected[4] = {};
    unsigned lastType = 0, headers = 0;
    for (size_t i = 0; i + 5 < videoEs.size(); ++i) {
        if (videoEs[i] || videoEs[i + 1] || videoEs[i + 2] != 1 || videoEs[i + 3]) continue;
        const unsigned type = (videoEs[i + 5] >> 3) & 7;
        lastType = type <= 3 ? type : 0;
        ++expected[lastType];
        ++headers;
    }
    assert(headers > 1 && lastType == trailingType);
    // The existing decoder requires a following picture start before trying
    // the current picture. hasStartCode restores the nonempty header cursor,
    // so this finite fixture stops before its last header, without an EOF
    // reference flush. Preserve that behavior rather than counting all headers
    // as decoded pictures or changing decoder semantics to satisfy this test.
    --expected[lastType];
    assert(expected[0] == 0 && expected[1] && expected[2] && expected[3]);
    for (unsigned skip = 0; skip < 2; ++skip) {
        mpeg::Video reference(videoEs), measured(videoEs);
        mpeg::DecodeDiagnostics totals;
        bool sawMultiplePictures = false, sawReferenceFlush = false;
        unsigned skippedSlots = 0;
        for (;;) {
            size_t a = 0, b = 0;
            bool skipped = false;
            const unsigned before = pictureAttempts(totals);
            const bool more = reference.decode(&a);
            assert(measured.decode(&b, skip != 0, &skipped, &totals) == more);
            const unsigned attempted = pictureAttempts(totals) - before;
            if (!more) break;
            sawMultiplePictures |= attempted > 1;
            sawReferenceFlush |= attempted == 0 && !skipped;
            assert(measured.lastFrameTime() == reference.lastFrameTime());
            if (skipped) {
                ++skippedSlots;
            } else {
                const auto* x = reference.frame(a);
                const auto* y = measured.frame(b);
                assert(x->y.data == y->y.data && x->cr.data == y->cr.data && x->cb.data == y->cb.data);
            }
        }
        assert(sawMultiplePictures && !sawReferenceFlush);
        const unsigned stoppedAttempts = pictureAttempts(totals);
        size_t unused = 0;
        bool skipped = false;
        assert(!measured.decode(&unused, skip != 0, &skipped, &totals));
        assert(pictureAttempts(totals) == stoppedAttempts && !skipped);
        for (unsigned type = 0; type < 4; ++type) {
            assert(totals.pictureAttempts[type] == (skip && type == 3 ? 0 : expected[type]));
#ifndef PSP
            assert(totals.pictureMicros[type] == 0 && totals.pictureMaxMicros[type] == 0);
#endif
        }
        assert(totals.skippedB == skippedSlots && skippedSlots == (skip ? expected[3] : 0));
#ifndef PSP
        assert(totals.skipMicros == 0 && totals.skipMaxMicros == 0);
#endif
    }
}
static void testPictureDiagnostics(const std::vector<u8>& videoEs) {
    checkPictureDiagnostics(videoEs, 3);
    // A header-only lookahead makes the fixture's final B picture eligible;
    // the incomplete extra header itself must not create an attempt or slot.
    std::vector<u8> withLookahead = videoEs;
    const u8 terminalHeader[] = {0, 0, 1, 0, 0, 0};
    withLookahead.insert(withLookahead.end(), terminalHeader, terminalHeader + sizeof(terminalHeader));
    checkPictureDiagnostics(withLookahead, 0);
    // A failed header read must not be counted as a reconstructed picture.
    mpeg::Video invalid{std::vector<u8>()};
    mpeg::DecodeDiagnostics empty;
    size_t index = 0;
    assert(!invalid.decode(&index, false, 0, &empty));
    assert(pictureAttempts(empty) == 0 && empty.skippedB == 0);
    std::puts("PASS: MPEG diagnostics match decodable I/P/B headers, distinguish multi-picture decode/B skips, preserve planes/time and do not count terminal lookahead");
}
static void testPresentationChanges(const std::vector<u8>& videoEs) {
    mpeg::VideoPlayer player(videoEs);
    std::vector<u32> pixels;
    assert(player.advanceAndRender(0,&pixels,320,240));
    const auto firstDiagnostic = player.advanceDiagnostics();
    assert(firstDiagnostic.decodeCalls == 1 && firstDiagnostic.slotsAdvanced == 1);
    assert(pictureAttempts(firstDiagnostic.pictures) > firstDiagnostic.slotsAdvanced);
    assert(firstDiagnostic.outputRequested && firstDiagnostic.wroteRgb && !firstDiagnostic.callerReduce);
    const auto first=pixels;
    assert(!player.advanceAndRender(0,&pixels,320,240) && pixels==first);
    const auto retained = player.advanceDiagnostics();
    assert(retained.decodeCalls == 0 && retained.slotsAdvanced == 0);
    assert(pictureAttempts(retained.pictures) == 0 && retained.pictures.skippedB == 0);
    assert(retained.outputRequested && !retained.wroteRgb);
    assert(!player.advanceAndRender(0,0,320,240));
    assert(!player.advanceDiagnostics().outputRequested && !player.advanceDiagnostics().wroteRgb);

    // A new destination must receive the retained frame even with no time advance.
    std::vector<u32> other;
    assert(player.advanceAndRender(0,&other,320,240) && other==pixels);
    assert(!player.advanceAndRender(0,&other,320,240));
    assert(player.advanceAndRender(0,&other,160,120) && other.size()==160*120);
    assert(!player.advanceAndRender(0,&other,160,120));
    // Replacing storage in the same vector also invalidates the retained target.
    std::vector<u32> replacement(other.size(),0x12345678u);
    other.swap(replacement);
    assert(player.advanceAndRender(0,&other,160,120));
    assert(!player.advanceAndRender(0,&other,160,120));

    for(unsigned tick=0;tick<12;++tick)assert(!player.advanceAndRender(1.0/30,0,160,120));
    assert(player.advanceAndRender(0,&other,160,120));
    assert(!player.advanceAndRender(0,&other,160,120));

    mpeg::VideoPlayer reduced(videoEs);
    std::vector<u32> held;
    unsigned retainedB=0,writes=0,ticks=0;
    while(!reduced.isFinished()) {
        const auto before=held;
        const unsigned skipped=reduced.skippedFrames();
        const bool changed=reduced.advanceAndRender(1.0/30,&held,320,240,true);
        const auto diagnostics = reduced.advanceDiagnostics();
        assert(diagnostics.pictures.skippedB == reduced.skippedFrames() - skipped);
        assert(diagnostics.pictures.pictureAttempts[3] == 0);
        assert(diagnostics.slotsAdvanced <= diagnostics.decodeCalls);
        assert(diagnostics.callerReduce && diagnostics.outputRequested && diagnostics.wroteRgb == changed);
        if(changed)++writes;
        else {
            assert(held==before);
            if(reduced.skippedFrames()>skipped && !held.empty())++retainedB;
        }
        assert(!reduced.advanceAndRender(0,&held,320,240,true));
        assert(reduced.advanceDiagnostics().decodeCalls == 0 && reduced.advanceDiagnostics().slotsAdvanced == 0);
        assert(pictureAttempts(reduced.advanceDiagnostics().pictures) == 0);
        assert(reduced.advanceDiagnostics().pictures.skippedB == 0 && !reduced.advanceDiagnostics().wroteRgb);
        assert(++ticks<10000);
    }
    assert(retainedB>0 && writes>0 && writes<ticks);
    // Completion retains pixels; rebinding still produces the last image once.
    std::vector<u32> last;
    assert(reduced.advanceAndRender(0,&last,320,240,true) && last==held);
    assert(!reduced.advanceAndRender(0,&last,320,240,true));
    std::puts("PASS: RGB-change signal excludes retained/B-skipped/no-output ticks and includes rebinding, resize and storage replacement");
}
int main(int argc, char** argv) {
    testWorkBudget();
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
    testPictureDiagnostics(streams.video);
    testPresentationChanges(streams.video);
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
