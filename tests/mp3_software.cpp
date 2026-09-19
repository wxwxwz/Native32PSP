#include "core/mp3_software.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <string>
using namespace n32;
int main() {
    for(const char* name:{"mp3-mpeg2-mono.mp3","mp3-mpeg1-stereo.mp3","mp3-mpeg25-mono.mp3"}) {
        std::string path=std::string("tests/fixtures/")+name;FILE* f=fopen(path.c_str(),"rb");assert(f);
        fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);std::vector<u8> bytes(size);
        assert(fread(bytes.data(),1,bytes.size(),f)==bytes.size());fclose(f);
        for(u32 rate:{11025u,22050u}) {
            auto pcm=decodeSoftwareMp3(bytes,rate);assert(pcm.size()>rate*2 && pcm.size()<rate*3);
            SoftwareMp3Stream stream(bytes,rate);
            std::vector<s16> streamed,block;size_t maxBlock=0;
            while(stream.readBlock(&block)) {maxBlock=std::max(maxBlock,block.size());streamed.insert(streamed.end(),block.begin(),block.end());}
            assert(streamed==pcm && maxBlock<=4608);
            assert(stream.retainedBytes()<bytes.size()+20000);
            stream.rewind();streamed.clear();
            while(stream.readBlock(&block))streamed.insert(streamed.end(),block.begin(),block.end());
            assert(streamed==pcm);
            size_t nonzero=0;for(s16 sample:pcm)if(sample)++nonzero;assert(nonzero>rate);
            bytes.resize(bytes.size()-1);assert(!decodeSoftwareMp3(bytes,rate).empty());
        }
    }
    for(size_t size:{0u,1u,10u,10000u})assert(decodeSoftwareMp3(std::vector<u8>(size,0),11025).empty());
    puts("PASS: real synthetic MPEG-1/2/2.5 MP3 decoding, mono/stereo, resampling, truncated tail and invalid input");
}
