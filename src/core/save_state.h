#ifndef NATIVE32_SAVE_STATE_H
#define NATIVE32_SAVE_STATE_H

#include "core/emulator.h"

namespace n32 {

static const size_t SerializedStateSize = 32u * 1024u * 1024u;
static const size_t SaveStateHeaderSize = 20u;

struct VideoState {
    std::string name;
    double elapsed;

    VideoState() : elapsed(0.0) {}
};

struct EmulatorState {
    std::string contentPath;
    u32 contentCrc32;
    u64 rngState;
    u64 tickCount;
    u32 timeMs;
    std::vector<std::string> pendingVideos;
    bool hasVideo;
    VideoState video;

    EmulatorState() : contentCrc32(0), rngState(0), tickCount(0), timeMs(0), hasVideo(false) {}
};

u32 crc32(const u8* data, size_t size);
bool encodeRawState(const std::vector<u8>& payload, std::vector<u8>* output);
bool decodeRawState(const std::vector<u8>& input, std::vector<u8>* payload);
size_t serializedSize();

}

#endif
