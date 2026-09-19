#include "core/mp3_software.h"
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"
#include <memory>
#include <limits.h>
namespace n32 {
struct SoftwareMp3Stream::State {
    std::vector<u8> input;
    mp3dec_t decoder;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t offset;
    u32 outputRate,rate;
    u64 boundary,next;
    s16 left,right;
    bool ended;
    State(const std::vector<u8>& bytes,u32 hz):input(bytes),outputRate(hz) { reset(); }
    void reset() { mp3dec_init(&decoder);offset=0;rate=0;boundary=next=0;left=right=0;ended=false; }
};
SoftwareMp3Stream::SoftwareMp3Stream(const std::vector<u8>& input,u32 rate):state(new State(input,rate)) {}
SoftwareMp3Stream::~SoftwareMp3Stream() {}
void SoftwareMp3Stream::rewind() { state->reset(); }
size_t SoftwareMp3Stream::retainedBytes() const { return sizeof(State)+state->input.capacity(); }
bool SoftwareMp3Stream::readBlock(std::vector<s16>* output) {
    if(!output)return false;
    output->clear();State& s=*state;
    if(s.ended || !s.outputRate || s.outputRate>48000 || s.input.size()>INT_MAX)return false;
    // One decoded MP3 frame per block; skip only a bounded number of frames
    // without PCM (e.g. the reservoir at stream start or malformed input).
    for(unsigned attempts=0;attempts<8 && s.offset<s.input.size();++attempts) {
        mp3dec_frame_info_t info={};
        int frames=mp3dec_decode_frame(&s.decoder,s.input.data()+s.offset,(int)(s.input.size()-s.offset),s.pcm,&info);
        if(info.frame_bytes<=0 || (size_t)info.frame_bytes>s.input.size()-s.offset) {s.offset=s.input.size();break;}
        s.offset+=info.frame_bytes;
        if(!frames)continue;
        if(info.hz<=0 || info.hz>48000 || (info.channels!=1 && info.channels!=2) || (s.rate && s.rate!=(u32)info.hz)) {
            s.ended=true;return false;
        }
        s.rate=info.hz;
        for(int i=0;i<frames;++i) {
            const s16 left=s.pcm[i*info.channels],right=s.pcm[i*info.channels+(info.channels==2?1:0)];
            while(s.next<=s.boundary) {
                const s32 fraction=s.boundary?(s32)(s.next-(s.boundary-s.outputRate)):(s32)s.outputRate;
                const s32 remaining=(s32)s.outputRate-fraction;
                output->push_back((s16)(((s32)s.left*remaining+(s32)left*fraction)/(s32)s.outputRate));
                output->push_back((s16)(((s32)s.right*remaining+(s32)right*fraction)/(s32)s.outputRate));
                s.next+=s.rate;
            }
            s.left=left;s.right=right;s.boundary+=s.outputRate;
        }
        if(!output->empty())return true;
    }
    // An exhausted/invalid stream finishes instead of spinning in the mixer.
    s.ended=true;
    while(s.rate && s.next<s.boundary) {
        output->push_back(s.left);output->push_back(s.right);s.next+=s.rate;
    }
    return !output->empty();
}
std::vector<s16> decodeSoftwareMp3(const std::vector<u8>& input, u32 outputRate) {
    std::vector<s16> output;
    if(input.empty() || input.size()>INT_MAX || !outputRate || outputRate>48000) return output;
    std::unique_ptr<mp3dec_t> decoder(new mp3dec_t);
    mp3dec_init(decoder.get());
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t offset=0;
    u32 rate=0; u64 boundary=0,next=0;
    s16 previousLeft=0,previousRight=0;
    const size_t limit=4*1024*1024;
    while(offset<input.size()) {
        mp3dec_frame_info_t info={};
        int frames=mp3dec_decode_frame(decoder.get(),input.data()+offset,(int)(input.size()-offset),pcm,&info);
        if(info.frame_bytes<=0) break;
        if((size_t)info.frame_bytes>input.size()-offset) return std::vector<s16>();
        offset+=info.frame_bytes;
        if(!frames) continue;
        if(info.hz<=0 || info.hz>48000 || (info.channels!=1 && info.channels!=2) || (rate && rate!=(u32)info.hz))
            return std::vector<s16>();
        rate=info.hz;
        for(int i=0;i<frames;++i) {
            s16 left=pcm[i*info.channels],right=pcm[i*info.channels+(info.channels==2?1:0)];
            while(next<=boundary) {
                if(output.size()+2>limit) return std::vector<s16>();
                s32 fraction=boundary?(s32)(next-(boundary-outputRate)):(s32)outputRate;
                s32 remaining=(s32)outputRate-fraction;
                output.push_back((s16)(((s32)previousLeft*remaining+(s32)left*fraction)/(s32)outputRate));
                output.push_back((s16)(((s32)previousRight*remaining+(s32)right*fraction)/(s32)outputRate));
                next+=rate;
            }
            previousLeft=left; previousRight=right; boundary+=outputRate;
        }
    }
    while(rate && next<boundary) {
        if(output.size()+2>limit) return std::vector<s16>();
        output.push_back(previousLeft); output.push_back(previousRight); next+=rate;
    }
    return output;
}
}
