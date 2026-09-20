// MPEG output geometry and buffer ownership, using generated frames and the
// existing synthetic bars/tone fixture. No game assets are needed.
#include "core/emulator.h"
#include "platform/game_presentation.h"
#include "platform/presentation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

using namespace n32;
namespace n32 { void pspLog(const char*, ...) {} }

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static u32 scalarRgb(u8 y, u8 cr, u8 cb) {
    const int yy = ((static_cast<int>(y) - 16) * 76309) >> 16;
    const int r = ((static_cast<int>(cr) - 128) * 104597) >> 16;
    const int b = ((static_cast<int>(cb) - 128) * 132201) >> 16;
    const int g = ((static_cast<int>(cb) - 128) * 25674 +
                   (static_cast<int>(cr) - 128) * 53278) >> 16;
    return 0xff000000u |
        (static_cast<u32>(std::max(0, std::min(255, yy + r))) << 16) |
        (static_cast<u32>(std::max(0, std::min(255, yy - g))) << 8) |
        static_cast<u32>(std::max(0, std::min(255, yy + b)));
}

static std::vector<u32> scalarImage(const mpeg::Frame& frame, size_t width, size_t height) {
    std::vector<u32> image(width * height);
    for (size_t y = 0; y < height; ++y) {
        const size_t sy = y * frame.height / height;
        for (size_t x = 0; x < width; ++x) {
            const size_t sx = x * frame.width / width;
            image[y * width + x] = scalarRgb(frame.y.data[sy * frame.y.width + sx],
                frame.cr.data[(sy / 2) * frame.cr.width + sx / 2],
                frame.cb.data[(sy / 2) * frame.cb.width + sx / 2]);
        }
    }
    return image;
}

class Bits {
public:
    std::vector<u8> data;
    void put(unsigned value, unsigned count) {
        while (count) {
            if (!used) data.push_back(0);
            data.back() |= static_cast<u8>(((value >> --count) & 1) << (7 - used));
            used = (used + 1) & 7;
        }
    }
    void start(u8 code) {
        if (used) put(0, 8 - used);
        data.push_back(0); data.push_back(0); data.push_back(1); data.push_back(code);
    }
private:
    unsigned used = 0;
};

// Four all-intra pictures with neutral Y/Cb/Cr=128. Each macroblock contains
// six DC-only blocks: size-zero DC and EOB. An incomplete final picture start
// supplies the decoder's existing lookahead without changing EOF semantics.
static std::vector<u8> solidVideo(unsigned width, unsigned height) {
    Bits bits;
    bits.start(0xb3);
    bits.put(width, 12); bits.put(height, 12);
    bits.put(1, 4); bits.put(3, 4); // Square pixels, 25 Hz.
    bits.put(2000, 18); bits.put(1, 1); bits.put(20, 10);
    bits.put(0, 1); bits.put(0, 1); bits.put(0, 1);
    for (unsigned picture = 0; picture < 4; ++picture) {
        bits.start(0x00);
        bits.put(picture, 10); bits.put(1, 3); bits.put(65535, 16); bits.put(0, 1);
        for (unsigned row = 0; row < (height + 15) / 16; ++row) {
            bits.start(static_cast<u8>(row + 1));
            bits.put(2, 5); bits.put(0, 1);
            for (unsigned column = 0; column < (width + 15) / 16; ++column) {
                bits.put(1, 1); bits.put(1, 1); // Address increment 1, intra macroblock.
                for (unsigned block = 0; block < 6; ++block) {
                    if (block < 4) bits.put(4, 3); // Luminance DC size 0: 100.
                    else bits.put(0, 2); // Chrominance DC size 0: 00.
                    bits.put(2, 2); // End of block: 10.
                }
            }
        }
    }
    bits.start(0x00);
    bits.put(0, 16);
    // Header parsing reserves enough bits for both optional quantizer matrices.
    bits.data.resize(bits.data.size() + 160, 0);
    return bits.data;
}

static void initGame(Emulator* em) {
    em->reader.width = 320; em->reader.height = 240;
    em->renderer.resize(320, 240);
    std::fill(em->renderer.buffer.begin(), em->renderer.buffer.end(), 0xff123456u);
    em->filename = "tests/fixtures/synthetic.smf";
}

static void bindVideo(Emulator* em, const std::vector<u8>& stream) {
    em->videoPlayer.reset(new mpeg::VideoPlayer(stream));
    require(em->videoPlayer->valid(), "generated video must have a valid sequence header");
    em->hasActiveVideo = true;
    em->activeVideoName = "generated-gray.mpg";
}

static void assertGeometry(const Emulator& em, size_t width, size_t height, bool video) {
    require(em.framebufferWidth() == width && em.framebufferHeight() == height,
            "framebuffer dimensions must match the current pixel source");
    require(em.framebuffer().size() == width * height, "framebuffer pixel count must match dimensions");
    require(em.isVideoFrame() == video, "video display ownership mismatch");
    require(em.gameWidth() == 320 && em.gameHeight() == 240 &&
            em.renderer.width == 320 && em.renderer.height == 240,
            "MPEG output must not resize the game canvas");
}

static void testOutputSizes() {
    const size_t sizes[][4] = {
        {0, 288, 0, 0}, {352, 0, 0, 0}, {1, 1, 1, 1},
        {160, 120, 160, 120}, {352, 288, 352, 288}, {480, 272, 480, 272},
        {511, 511, 511, 511}, {512, 512, 512, 512},
        {513, 512, 272, 272}, {512, 513, 271, 272},
        {640, 360, 480, 270}, {720, 576, 340, 272},
        {4095, 4095, 272, 272}, {4095, 1, 480, 1}, {1, 4095, 1, 272}
    };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t width = 0, height = 0;
        mpeg::videoOutputSize(sizes[i][0], sizes[i][1], &width, &height);
        require(width == sizes[i][2] && height == sizes[i][3], "native/bounded MPEG output geometry");
    }
    require(videoScaling(160, 120) == ScaleOriginal && videoScaling(480, 272) == ScaleOriginal,
            "screen-fitting videos must not be enlarged");
    u32 width = 0, height = 0;
    scaledSize(352, 288, videoScaling(352, 288), &width, &height);
    require(width == 332 && height == 272, "352x288 must fit the whole video instead of cropping");
    std::puts("PASS: native MPEG geometry, 512 texture boundary, bounded fallback and no-enlargement presentation");
}

static void testGeneratedFrames() {
    const unsigned dimensions[][2] = {
        {1, 1}, {17, 13}, {160, 120}, {352, 288}, {480, 272},
        {512, 512}, {513, 512}, {512, 513}, {720, 576}, {4095, 1}
    };
    for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        const unsigned width = dimensions[i][0], height = dimensions[i][1];
        const std::vector<u8> stream = solidVideo(width, height);
        mpeg::Video decoder(stream);
        size_t frameIndex = 0;
        require(decoder.hasHeader() && decoder.decode(&frameIndex), "generated intra stream must decode");
        const mpeg::Frame& frame = *decoder.frame(frameIndex);
        const std::vector<u32> native = scalarImage(frame, width, height);
        require(native == std::vector<u32>(static_cast<size_t>(width) * height, scalarRgb(128, 128, 128)),
                "generated macroblocks must cover the complete logical image with neutral gray");

        mpeg::VideoPlayer player(stream);
        require(player.width() == width && player.height() == height, "player exposes decoded dimensions");
        std::vector<u32> output;
        require(player.advanceAndRender(0, &output, player.width(), player.height()), "first native output");
        require(output == native, "native player output must match decoded-plane scalar oracle");
        require(!player.advanceAndRender(0, &output, width, height), "retained native frame must not reconvert");

        Emulator em;
        initGame(&em);
        const std::vector<u32> game = em.renderer.buffer;
        bindVideo(&em, stream);
        em.tick(false);
        assertGeometry(em, 320, 240, false);
        require(!em.frameChanged && em.framebuffer() == game, "suppressed first RGB retains game pixels");
        em.tick(true);
        size_t outputW, outputH;
        mpeg::videoOutputSize(width, height, &outputW, &outputH);
        assertGeometry(em, outputW, outputH, true);
        require(em.frameChanged && em.framebuffer() == scalarImage(frame, outputW, outputH),
                "core uses native or bounded RGB directly, without the game canvas");
        require(em.renderer.buffer == game, "cutscene writes must leave game framebuffer untouched");
        const auto held = em.framebuffer();
        em.tick(false);
        require(!em.frameChanged && em.framebuffer() == held, "suppressed RGB keeps the native display frame");
        unsigned ticks = 0;
        while (em.videoPlayer && ++ticks < 30) em.tick(false);
        require(!em.videoPlayer && ticks < 30, "generated clip reaches EOF");
        assertGeometry(em, outputW, outputH, true);
        require(em.framebuffer() == held, "EOF retains last pixels and geometry after player destruction");
        em.tick(false);
        assertGeometry(em, outputW, outputH, true);
        em.drawCurrentFrame();
        assertGeometry(em, 320, 240, false);
    }
    std::puts("PASS: generated MPEG native RGB, source-size accessors, game isolation, frame skip, EOF retention and game restoration");
}

static void testLifecycle(const std::vector<u8>& fixtureVideo) {
    Emulator em;
    initGame(&em);
    bindVideo(&em, solidVideo(352, 288));
    em.tick(true);
    assertGeometry(em, 352, 288, true);
    const std::vector<u32> first = em.framebuffer();
    const std::vector<u32> game = em.renderer.buffer;
    em.pendingVideos.push_back("mpeg-test.mpg");
    require(em.skipCutscene(), "skip active video with another queued");
    require(!em.videoPlayer && em.pendingVideos.size() == 1 && em.framebuffer() == first,
            "skip must retain last RGB while waiting for next clip");
    assertGeometry(em, 352, 288, true);
    em.tick(false);
    require(em.videoPlayer && em.videoPlayer->width() == 160 && em.videoPlayer->height() == 120,
            "queued fixture should start at its own decoded dimensions");
    require(em.framebuffer() == first, "new clip without RGB must retain previous clip pixels");
    assertGeometry(em, 352, 288, true);
    mpeg::VideoPlayer reference(fixtureVideo);
    std::vector<u32> expected;
    reference.advanceAndRender(1.0 / 30, 0, 160, 120);
    reference.advanceAndRender(1.0 / 30, &expected, 160, 120);
    em.tick(true);
    assertGeometry(em, 160, 120, true);
    require(em.framebuffer() == expected && em.renderer.buffer == game,
            "next clip replaces both dimensions and pixels on its first RGB write");
    require(em.skipCutscene() && !em.isCutsceneActive(), "skip final clip");
    assertGeometry(em, 320, 240, false);
    require(em.frameChanged, "final skip must redraw the game canvas");

    bindVideo(&em, solidVideo(352, 288));
    em.tick(true);
    require(em.isVideoFrame(), "new native frame before reset");
    em.reset();
    require(!em.isVideoFrame() && em.framebuffer().empty() && !em.videoPlayer,
            "reset releases video display state");

    initGame(&em);
    bindVideo(&em, solidVideo(352, 288));
    em.tick(true);
    require(!em.loadFromPath("tests/out/nonexistent-native-video-game.smf", 100), "missing game reload fails");
    require(!em.isVideoFrame() && !em.videoPlayer, "failed content load cannot keep stale video ownership");

    initGame(&em);
    bindVideo(&em, solidVideo(352, 288));
    em.tick(true);
    em.pendingVideos.push_back("no-such-native-video.mpg");
    em.pendingVideos.push_back("mpeg-test.mpg");
    require(em.skipCutscene(), "skip before failed queued clip");
    const auto retained = em.framebuffer();
    em.tick(true);
    require(!em.videoPlayer && em.pendingVideos.size() == 1 && em.framebuffer() == retained,
            "failed queued clip retains display and advances the queue once");
    em.tick(true);
    assertGeometry(em, 160, 120, true);
    std::puts("PASS: differently sized queued clips, first suppressed frame, skip, missing clip, reset and failed reload ownership");
}

static mpeg::Frame benchmarkFrame() {
    mpeg::Frame frame;
    frame.width = 352; frame.height = 288;
    mpeg::Plane* planes[] = {&frame.y, &frame.cr, &frame.cb};
    unsigned seed = 0x19283746;
    for (unsigned index = 0; index < 3; ++index) {
        mpeg::Plane& plane = *planes[index];
        plane.width = (index ? 176 : 352) + 5 + index * 3;
        plane.height = index ? 144 : 288;
        plane.data.resize(plane.width * plane.height);
        for (size_t i = 0; i < plane.data.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            plane.data[i] = static_cast<u8>(seed >> 24);
        }
    }
    return frame;
}

static __attribute__((noinline)) void convert(const mpeg::Frame& frame,
    std::vector<u32>* output, size_t width, size_t height) {
    frame.writeRgbScaled(output, width, height);
}

static void benchmark(bool native, bool prepare) {
    mpeg::Frame frame = benchmarkFrame();
    const size_t width = native ? 352 : 320, height = native ? 288 : 240;
    const unsigned iterations = 2048;
    std::vector<u32> output(width * height);
    GamePresentation presentation;
    for (unsigned i = 0; i < 16; ++i) {
        convert(frame, &output, width, height);
        if (prepare) require(presentation.prepare(output, width, height, ScaleFit, false), "benchmark prepare warmup");
    }
    require(output == scalarImage(frame, width, height), "benchmark warmup pixels");
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        frame.y.data[i % frame.y.data.size()] = static_cast<u8>(i * 37);
        convert(frame, &output, width, height);
        if (prepare) presentation.prepare(output, width, height, ScaleFit, false);
    }
    const auto end = std::chrono::steady_clock::now();
    require(output == scalarImage(frame, width, height), "benchmark final pixels");
    uint64_t hash = UINT64_C(14695981039346656037);
    for (u32 pixel : output) { hash ^= pixel; hash *= UINT64_C(1099511628211); }
    const long long micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::printf("%s_%s source=352x288 output=%zux%zu iterations=%u us=%lld us_per_frame=%.3f rgb_bytes=%zu surface_bytes=%zu checksum=%016llx\n",
                native ? "native" : "game", prepare ? "rgb_prepare" : "rgb",
                width, height, iterations, micros,
                static_cast<double>(micros) / iterations, output.size() * sizeof(u32),
                presentation.retainedBytes(),
                static_cast<unsigned long long>(hash));
}

int main(int argc, char** argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--bench-native") || !std::strcmp(argv[1], "--bench-game"))) {
        const bool native = !std::strcmp(argv[1], "--bench-native");
        benchmark(native, false);
        benchmark(native, true);
        return 0;
    }
    require(argc <= 2, "usage: mpeg-native [fixture.mpg|--bench-native|--bench-game]");
    const char* path = argc == 2 ? argv[1] : "tests/fixtures/mpeg-test.mpg";
    std::vector<u8> bytes;
    require(readWholeFile(path, &bytes), "read synthetic bars/tone fixture");
    const mpeg::DemuxedStreams streams = mpeg::demuxAll(bytes);
    testOutputSizes();
    testGeneratedFrames();
    testLifecycle(streams.video);
    std::puts("PASS: MPEG native output and bounded presentation regression");
    return 0;
}
