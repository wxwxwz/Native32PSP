#include "core/load_progress.h"
#ifndef NATIVE32_MPEG_DEMUX_H
#define NATIVE32_MPEG_DEMUX_H

#include <psptypes.h>
#include <vector>
#include "core/mpeg/buffer.h"

namespace n32 {
namespace mpeg {

static const int PACKET_PRIVATE = 0xbd;
static const int PACKET_AUDIO_1 = 0xc0;
static const int PACKET_AUDIO_4 = 0xc3;
static const int PACKET_VIDEO_1 = 0xe0;
static const double INVALID_TS = -1.0;

struct Packet {
    s32 type;
    double pts;
    std::vector<u8> data;
    const u8* payload; // View valid until the next decode on the owning Demux.
    size_t payloadSize;

    Packet() : type(-1), pts(INVALID_TS), payload(0), payloadSize(0) {}
};

struct DemuxedStreams {
    std::vector<u8> video;
    std::vector<u8> audio;
    s32 numVideoStreams;
    s32 numAudioStreams;

    DemuxedStreams() : numVideoStreams(0), numAudioStreams(0) {}
};

// MPEG-PS demuxer that splits an MPEG-1 program stream into elementary video
// and audio streams. Faithful port of the Java/Rust Demux reference.
class Demux {
public:
    explicit Demux(std::vector<u8> data, bool copyPayload = true);
    explicit Demux(Buffer input, bool copyPayload=false);
    void rewind();

    s32 numVideoStreams() const { return mNumVideoStreams; }
    s32 numAudioStreams() const { return mNumAudioStreams; }

    bool hasHeaders();
    bool decode(Packet* out);
    size_t position() const { return buffer.bitIndex>>3; }

private:
    double decodeTime();
    bool decodePacket(int type, Packet* out);
    bool getPacket(Packet* out);

    Buffer buffer;
    bool copyPayload;
    int startCode;
    bool hasPackHeader;
    bool hasSystemHeader;
    bool hasHeadersFlag;
    int mNumAudioStreams;
    int mNumVideoStreams;
    int currentLength;
    int nextType;
    int nextLength;
    double nextPts;
};

std::shared_ptr<ByteSource> openProgramStream(const std::string& path,int packetType);

DemuxedStreams demuxAll(std::vector<u8> data, const LoadProgress& progress=LoadProgress(), const std::string& path="");

}
}

#endif
