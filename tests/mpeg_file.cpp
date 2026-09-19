// External user-supplied MPEG probe; no game assets are included in the repo.
#include "core/mpeg/demux.h"
#include "core/mpeg/video.h"
#include "core/mpeg/audio.h"
#include <cstdio>
#include <cassert>
#include <utility>
#include <sys/resource.h>
#include <chrono>
using namespace n32;
static unsigned long long hash = 1469598103934665603ull;
static void record(u32 x) { hash = (hash ^ x) * 1099511628211ull; }
int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    FILE* file = std::fopen(argv[1], "rb"); assert(file);
    std::fseek(file, 0, SEEK_END); long size = std::ftell(file); assert(size > 0);
    std::rewind(file); std::vector<u8> data(size);
    assert(std::fread(data.data(), 1, data.size(), file) == data.size()); std::fclose(file);
    mpeg::DemuxedStreams streams = mpeg::demuxAll(std::move(data));
    std::vector<u8>().swap(data);
    std::printf("streams: video=%zu audio=%zu\n", streams.video.size(), streams.audio.size());
    for (u8 byte : streams.video) record(byte);
    for (u8 byte : streams.audio) record(byte);
    mpeg::Video video(std::move(streams.video));
    mpeg::Audio audio(std::move(streams.audio));
    assert(video.hasHeader() && audio.hasHeader());
    std::vector<u8>().swap(streams.video); std::vector<u8>().swap(streams.audio);
    size_t frame = 0, videoFrames = 0, audioFrames = 0;
    std::vector<u32> pixels;
    auto begin = std::chrono::steady_clock::now();
    while (video.decode(&frame)) {
        assert(++videoFrames < 20000);
        video.frame(frame)->writeRgbScaled(&pixels, 320, 240);
        for (size_t i = 0; i < pixels.size(); i += 127) record(pixels[i]);
    }
    auto videoEnd = std::chrono::steady_clock::now();
    std::printf("video_digest=%016llx video_us=%lld\n", hash,
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(videoEnd - begin).count());
    FILE* dump = argc == 3 ? std::fopen(argv[2], "wb") : 0;
    assert(argc != 3 || dump);
    mpeg::Samples pcm;
    while (audio.decode(&pcm)) {
        assert(++audioFrames < 100000);
        if (dump) assert(std::fwrite(pcm.interleaved.data(), sizeof(float),
            pcm.interleaved.size(), dump) == pcm.interleaved.size());
        for (size_t i = 0; i < pcm.interleaved.size(); i += 31)
            record((u32)(s32)(pcm.interleaved[i] * 32767.0f));
    }
    if (dump) std::fclose(dump);
    std::printf("audio_us=%lld\n", (long long)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - videoEnd).count());
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    assert(videoFrames && audioFrames);
    std::printf("PASS video_frames=%zu audio_frames=%zu digest=%016llx peak_rss_kb=%ld\n",
                videoFrames, audioFrames, hash, usage.ru_maxrss);
}
