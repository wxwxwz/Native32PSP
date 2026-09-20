#include "core/mpeg/demux.h"
#include <utility>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace n32 {
namespace mpeg {

static const int START_PACK = 0xba;
static const int START_SYSTEM = 0xbb;

Demux::Demux(std::vector<u8> data, bool copy) : Demux(Buffer(std::move(data)),copy) {}

Demux::Demux(Buffer input,bool copy)
    : buffer(std::move(input)), copyPayload(copy), startCode(-1), hasPackHeader(false), hasSystemHeader(false),
      hasHeadersFlag(false), mNumAudioStreams(0), mNumVideoStreams(0), currentLength(0),
      nextType(-1), nextLength(0), nextPts(INVALID_TS) {
    hasHeaders();
}

void Demux::rewind() {
    buffer.seek(0);
    startCode = -1;
    hasPackHeader = hasSystemHeader = hasHeadersFlag = false;
    mNumAudioStreams = mNumVideoStreams = currentLength = nextLength = 0;
    nextType = -1;
    nextPts = INVALID_TS;
}

bool Demux::hasHeaders() {
    if (hasHeadersFlag) {
        return true;
    }

    if (!hasPackHeader) {
        if (startCode != START_PACK && buffer.findStartCode(START_PACK) == -1) {
            return false;
        }
        startCode = START_PACK;
        if (!buffer.has(64)) {
            return false;
        }
        startCode = -1;
        if (buffer.read(4) != 0x02) {
            return false;
        }
        decodeTime();
        buffer.skip(1);
        buffer.skip(22);
        buffer.skip(1);
        hasPackHeader = true;
    }

    if (!hasSystemHeader) {
        if (startCode != START_SYSTEM && buffer.findStartCode(START_SYSTEM) == -1) {
            return false;
        }
        startCode = START_SYSTEM;
        if (!buffer.has(56)) {
            return false;
        }
        startCode = -1;
        buffer.skip(16);
        buffer.skip(24);
        mNumAudioStreams = (int)buffer.read(6);
        buffer.skip(5);
        mNumVideoStreams = (int)buffer.read(5);
        hasSystemHeader = true;
    }

    hasHeadersFlag = true;
    return true;
}

bool Demux::decode(Packet* out) {
    buffer.discardReadBytes();
    if (!hasHeaders()) {
        return false;
    }

    if (currentLength > 0) {
        size_t bits = (size_t)currentLength << 3;
        if (!buffer.has(bits)) {
            return false;
        }
        buffer.skip(bits);
        currentLength = 0;
    }

    if (startCode != -1) {
        return decodePacket(startCode, out);
    }

    for (;;) {
        startCode = (int)buffer.nextStartCode();
        int sc = startCode;
        if (sc == PACKET_VIDEO_1 || sc == PACKET_PRIVATE ||
            (sc >= PACKET_AUDIO_1 && sc <= PACKET_AUDIO_4)) {
            return decodePacket(sc, out);
        }
        if (sc == -1) {
            return false;
        }
    }
}

double Demux::decodeTime() {
    long long clock = (long long)buffer.read(3) << 30;
    buffer.skip(1);
    clock |= (long long)buffer.read(15) << 15;
    buffer.skip(1);
    clock |= buffer.read(15);
    buffer.skip(1);
    return (double)clock / 90000.0;
}

bool Demux::decodePacket(int type, Packet* out) {
    if (!buffer.has(16 << 3)) {
        return false;
    }
    startCode = -1;
    nextType = type;

    int length = (int)buffer.read(16);
    length -= (int)buffer.skipBytes(0xff);

    if (buffer.read(2) == 0x01) {
        buffer.skip(16);
        length -= 2;
    }

    int ptsDtsMarker = (int)buffer.read(2);
    if (ptsDtsMarker == 0x03) {
        nextPts = decodeTime();
        buffer.skip(40);
        length -= 10;
    } else if (ptsDtsMarker == 0x02) {
        nextPts = decodeTime();
        length -= 5;
    } else if (ptsDtsMarker == 0x00) {
        nextPts = INVALID_TS;
        buffer.skip(4);
        length -= 1;
    } else {
        return false;
    }

    if (length < 0) {
        return false;
    }
    nextLength = length;
    return getPacket(out);
}

bool Demux::getPacket(Packet* out) {
    size_t bits = (size_t)nextLength << 3;
    if (!buffer.has(bits)) {
        return false;
    }
    if (out) {
        out->type = nextType;
        out->pts = nextPts;
        out->payload = buffer.data.data() + (buffer.bitIndex >> 3);
        out->payloadSize = (size_t)nextLength;
        if(copyPayload) out->data.assign(out->payload,out->payload+out->payloadSize);
        else out->data.clear();
    }
    currentLength = nextLength;
    nextLength = 0;
    return true;
}

namespace {
class FileSource : public ByteSource {
public:
    FILE* file;
    explicit FileSource(const std::string& path):file(fopen(path.c_str(),"rb")){}
    ~FileSource(){if(file)fclose(file);}
    size_t read(u8* out,size_t capacity){return file?fread(out,1,capacity,file):0;}
};
class PacketSource : public ByteSource {
public:
    Demux demux;Packet packet;size_t offset;int type;
    PacketSource(std::shared_ptr<ByteSource> file,int stream):demux(Buffer(file),false),offset(0),type(stream){}
    size_t read(u8* out,size_t capacity) {
        if(!capacity)return 0;
        while(offset==packet.payloadSize) {
            if(!demux.decode(&packet))return 0;
            offset=0;
            if(packet.type!=type){offset=packet.payloadSize;continue;}
            if(!packet.payloadSize)continue;
            break;
        }
        // Return available payload promptly. Filling a 16 KiB ES request used
        // to scan many interleaved packets, especially for low-bitrate audio,
        // even when the decoder needed only its next frame. Buffer::has will
        // ask again if this packet is insufficient; no playback data is lost.
        size_t n=std::min(capacity,packet.payloadSize-offset);
        memcpy(out,packet.payload+offset,n);offset+=n;
        return n;
    }
};
}
std::shared_ptr<ByteSource> openProgramStream(const std::string& path,int type) {
    std::shared_ptr<FileSource> file(new FileSource(path));
    if(!file->file)return std::shared_ptr<ByteSource>();
    return openProgramStream(file,type);
}
std::shared_ptr<ByteSource> openProgramStream(std::shared_ptr<ByteSource> input,int type) {
    if(!input)return std::shared_ptr<ByteSource>();
    std::shared_ptr<PacketSource> stream(new PacketSource(input,type));
    if(!stream->demux.hasHeaders())return std::shared_ptr<ByteSource>();
    // Legacy discs may declare audio_bound=0 despite containing C0 packets.
    // Header counts are advisory; let packet discovery determine actual audio.
    return stream;
}

DemuxedStreams demuxAll(std::vector<u8> data,const LoadProgress& progress,const std::string& path) {
    const size_t total=data.size();size_t lastReport=0;
    progress.report("Indexing video",path,0,total);
    DemuxedStreams out;
    Demux demux(std::move(data), false);
    Packet packet;
    // Count first to avoid geometric capacity growth (and its temporary old
    // allocation) while the whole program stream is still in memory.
    size_t videoBytes = 0, audioBytes = 0;
    while (demux.decode(&packet)) {
        if(demux.position()-lastReport>=131072) {
            lastReport=demux.position();progress.report("Indexing video",path,std::min(lastReport,total),total);
        }
        if (packet.type == PACKET_VIDEO_1) videoBytes += packet.payloadSize;
        else if (packet.type == PACKET_AUDIO_1) audioBytes += packet.payloadSize;
    }
    out.video.reserve(videoBytes);
    out.audio.reserve(audioBytes);
    demux.rewind();lastReport=0;
    progress.report("Separating streams",path,0,total);
    while (demux.decode(&packet)) {
        if(demux.position()-lastReport>=131072) {
            lastReport=demux.position();progress.report("Separating streams",path,std::min(lastReport,total),total);
        }
        if (packet.type == PACKET_VIDEO_1) {
            out.video.insert(out.video.end(), packet.payload, packet.payload+packet.payloadSize);
            out.numVideoStreams = 1;
        } else if (packet.type == PACKET_AUDIO_1) {
            out.audio.insert(out.audio.end(), packet.payload, packet.payload+packet.payloadSize);
            out.numAudioStreams = 1;
        }
    }
    return out;
}

}
}
