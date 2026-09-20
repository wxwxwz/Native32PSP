#include "core/mpeg/demux.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace n32;

class CountedFile : public mpeg::ByteSource {
public:
    explicit CountedFile(const std::vector<u8>& bytes):data(bytes),position(0),calls(0) {}
    size_t read(u8* out,size_t capacity) {
        ++calls;size_t count=std::min(capacity,data.size()-position);
        if(count)memcpy(out,data.data()+position,count);
        position+=count;return count;
    }
    const std::vector<u8>& data;
    size_t position,calls;
};

static std::vector<u8> drain(const std::vector<u8>& bytes,int type) {
    auto input=std::make_shared<CountedFile>(bytes);
    auto packets=mpeg::openProgramStream(input,type);assert(packets);
    mpeg::Buffer es(packets);std::vector<u8> result;
    while(es.has(8)) {
        size_t size=std::min((size_t)701,es.data.size()-(es.bitIndex>>3));
        result.insert(result.end(),es.data.begin()+(es.bitIndex>>3),es.data.begin()+(es.bitIndex>>3)+size);
        es.skip(size*8);es.discardReadBytes();
        assert(es.data.capacity()<=mpeg::Buffer::streamLimit);
    }
    size_t calls=input->calls;
    assert(!es.has(8) && !es.has(1) && input->calls==calls);
    return result;
}

int main(int argc,char** argv) {
    assert(argc==2);
    FILE* file=fopen(argv[1],"rb");assert(file);
    fseek(file,0,SEEK_END);long length=ftell(file);assert(length>0);rewind(file);
    std::vector<u8> bytes((size_t)length);
    assert(fread(bytes.data(),1,bytes.size(),file)==bytes.size());fclose(file);
    assert(!mpeg::openProgramStream(std::shared_ptr<mpeg::ByteSource>(),mpeg::PACKET_AUDIO_1));
    const auto reference=mpeg::demuxAll(bytes);
    for(int type:{mpeg::PACKET_AUDIO_1,mpeg::PACKET_VIDEO_1}) {
        // Locate the first useful packet independently in the entire PS file.
        mpeg::Demux packetReader(bytes,false);mpeg::Packet packet;
        while(packetReader.decode(&packet))if(packet.type==type && packet.payloadSize)break;
        assert(packet.type==type && packet.payloadSize && packet.payloadSize<16384);
        std::vector<u8> first(packet.payload,packet.payload+packet.payloadSize);
        const size_t packetEnd=packetReader.position()+packet.payloadSize;
        auto counted=std::make_shared<CountedFile>(bytes);
        auto packets=mpeg::openProgramStream(counted,type);assert(packets);
        size_t before=counted->position;assert(packets->read(0,0)==0 && counted->position==before);
        mpeg::Buffer elementary(packets);
        assert(elementary.has(8));
        // Asking for a bit may fill the file's 16 KiB read-ahead, but must not
        // gather seconds of elementary audio before returning the first packet.
        assert(elementary.data==first);
        assert(counted->position<=std::min(bytes.size(),((packetEnd+16+16383)/16384)*16384));
        printf("stream=%02x first_payload=%zu first_file_read=%zu\n",type,first.size(),counted->position);
        const auto& expected=type==mpeg::PACKET_AUDIO_1?reference.audio:reference.video;
        size_t total=0;
        while(elementary.has(8)) {
            size_t take=std::min((size_t)997,elementary.data.size()-(elementary.bitIndex>>3));
            assert(take && total+take<=expected.size());
            assert(std::equal(elementary.data.begin()+(elementary.bitIndex>>3),
                elementary.data.begin()+(elementary.bitIndex>>3)+take,expected.begin()+total));
            total+=take;elementary.skip(take*8);elementary.discardReadBytes();
            assert(elementary.data.capacity()<=mpeg::Buffer::streamLimit);
        }
        assert(total==expected.size());
        if(type==mpeg::PACKET_AUDIO_1) {
            auto truncated=bytes;truncated.resize(packetEnd-first.size()/2);
            assert(drain(truncated,type)==mpeg::demuxAll(truncated).audio);
            // Invalid PTS marker after one valid audio packet. Match demuxAll:
            // retain earlier payload and terminate on the malformed packet.
            std::vector<u8> malformed(bytes.begin(),bytes.begin()+packetEnd);
            const u8 invalid[]={0,0,1,0xc0,0,1,0x10,0,0,0,0,0,0,0,0,0,0,0,0,0};
            malformed.insert(malformed.end(),invalid,invalid+sizeof(invalid));
            malformed.insert(malformed.end(),bytes.begin()+packetEnd,bytes.end());
            auto stopped=mpeg::demuxAll(malformed).audio;
            assert(stopped==first && drain(malformed,type)==stopped);
        }
    }
    assert(drain(bytes,mpeg::PACKET_AUDIO_4).empty());
    puts("PASS: first packet returns without collecting a full ES block; all interleaved audio/video bytes and bounds preserved");
    puts("PASS: truncated/malformed PES retain prior payload then end; absent tracks and repeated EOF terminate safely");
}
