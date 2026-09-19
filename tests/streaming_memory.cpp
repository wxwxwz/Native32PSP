#include "core/mp3_software.h"
#include "core/mpeg/audio.h"
#include "core/mpeg/demux.h"
#include <cstdio>
#include <cassert>
#include <chrono>
#include <cstring>
#include <algorithm>
using namespace n32;
static std::vector<u8> read(const char* name) {FILE* f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);std::vector<u8> b(n);assert(fread(b.data(),1,n,f)==n);fclose(f);return b;}
int main(int argc,char** argv) {
    assert(argc==2);
#ifndef N32_MP2_ONLY
    if(argv[1][0]=='m') {
        auto data=read("tests/fixtures/mp3-long60.mp3");
        auto full=decodeSoftwareMp3(data,11025);SoftwareMp3Stream stream(data,11025);std::vector<s16> block;
        size_t offset=0,peak=0;while(stream.readBlock(&block)) {
            peak=std::max(peak,stream.retainedBytes()+block.capacity()*2);
            assert(offset+block.size()<=full.size());
            for(size_t i=0;i<block.size();++i)assert(full[offset+i]==block[i]);offset+=block.size();
        }
        assert(offset==full.size() && peak<full.capacity()*2/4);
        printf("60s MP3: full_pcm_capacity=%u streaming_total=%u pcm_identical=yes\n",(unsigned)(full.capacity()*2),(unsigned)peak);
    } else
#endif
    {
        auto data=read("tests/fixtures/mpeg-test.mpg");auto streams=mpeg::demuxAll(std::move(data));
        mpeg::Audio audio(std::move(streams.audio));mpeg::Samples samples;u64 hash=1469598103934665603ull;size_t n=0;
        while(audio.decode(&samples)) {for(float v:samples.interleaved){u32 bits;memcpy(&bits,&v,4);hash=(hash^bits)*1099511628211ull;}n+=samples.interleaved.size();}
        printf("MP2 samples=%u digest=%016llx\n",(unsigned)n,(unsigned long long)hash);
    }
}
