#include "core/audio_engine.h"
#include "core/renderer.h"
#include "core/mpeg/video.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

// Count host allocations independently of wall time (which is not PSP FPS).
static size_t allocations;
#ifndef N32_SANITIZE
void* operator new(size_t size) {
    ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#endif

using namespace n32;
static unsigned long long digest = 1469598103934665603ull;
template<class T> void record(const std::vector<T>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        digest ^= (u32)values[i];
        digest *= 1099511628211ull;
    }
}

static RgbaImage solid(u32 w, u32 h, u32 color) {
    RgbaImage image;
    image.width = w;
    image.height = h;
    image.pixels.assign((size_t)w * h, color);
    return image;
}

static void testClipping() {
    RgbaImage image = solid(7, 5, 0);
    for (size_t i = 0; i < image.pixels.size(); ++i)
        image.pixels[i] = (i % 3 ? 0x01000000u : 0) | (u32)i;
    for (int truncated = 0; truncated < 2; ++truncated) {
        if (truncated) image.pixels.resize(19);
        for (int y = -8; y <= 12; ++y) for (int x = -10; x <= 14; ++x) {
            Renderer renderer(11, 9);
            std::vector<u32> expected = renderer.buffer;
            // Intentionally slow per-pixel reference, independent of clipping.
            for (size_t i = 0; i < image.pixels.size(); ++i) {
                int dx = x + (int)(i % image.width);
                int dy = y + (int)(i / image.width);
                if (dx >= 0 && dx < 11 && dy >= 0 && dy < 9 && (image.pixels[i] >> 24))
                    expected[dy * 11 + dx] = image.pixels[i];
            }
            renderer.blitImage(image, x, y);
            assert(renderer.buffer == expected);
            record(renderer.buffer);
        }
    }
}

static void testDrawOrder() {
    Native32Reader reader;
    SpriteSystem sprites;
    Renderer renderer(4, 4);
    std::vector<FrameObject> objects;
    const std::string a(50, 'a'), b(50, 'b');
    sprites.insert(a, MovieState(0, 0, 0, 5));
    sprites.insert(b, MovieState(0, 0, 0, 5));
    renderer.setSpriteOverride(a, solid(4, 4, 0xff123456));
    renderer.setSpriteOverride(b, solid(4, 4, 0x01010203), "leader");
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[0] == 0x01010203); // Equal depth: map order wins.
    sprites.insert("leader", MovieState());
    sprites.getMutable("leader")->visible = false;
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[0] == 0xff123456);
    sprites.getMutable("leader")->visible = true;
    sprites.getMutable(a)->depth = 6;
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[0] == 0xff123456);
    sprites.getMutable(a)->visible = false;
    renderer.screenX = -1;
    renderer.screenY = 1;
    renderer.setSpriteOverride(b, solid(4, 4, 0xffabcdef));
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[0] == 0xff000000);
    assert(renderer.buffer[4] == 0xffabcdef);
    assert(renderer.buffer[7] == 0xff000000);
    record(renderer.buffer);
    renderer.clearSpriteOverrides();
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer == std::vector<u32>(16, 0xff000000));
    // Cached movie offsets must still apply to overrides.
    reader.data.assign(28, 0);
    reader.data[0] = 4;
    reader.data[4] = 1;
    reader.data[6] = 1;
    reader.data[8] = 2;
    sprites.getMutable(b)->movie = 1;
    renderer.screenX = renderer.screenY = 0;
    renderer.setSpriteOverride(b, solid(1, 1, 0xff102030));
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[9] == 0xff102030);
    record(renderer.buffer);
}

static std::vector<u8> pcm(size_t frames, s16 value) {
    std::vector<u8> bytes(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        bytes[i * 2] = (u16)value & 255;
        bytes[i * 2 + 1] = (u16)value >> 8;
    }
    return bytes;
}

static void testAudio() {
    AudioEngine audio(ColorspaceYuv, 100);
    size_t shortId = audio.playRaw(pcm(1, 100), 1, "short");
    size_t loopId = audio.playRaw(pcm(4, 1000), 255, "loop");
    std::vector<s16> out = audio.getPendingSamples();
    assert(out.size() == 367 * 2);
    assert(out[0] == 1100 && out[2] == 1100 && out[4] == 1000);
    assert(!audio.isChannelPlaying(shortId) && audio.isChannelPlaying(loopId));
    record(out);
    size_t total = 367;
    for (int i = 1; i < 30; ++i) {
        out = audio.getPendingSamples();
        total += out.size() / 2;
        assert(std::count(out.begin(), out.end(), (s16)1000) == (int)out.size());
        record(out);
    }
    assert(total == 11025);
    audio.stopForMovie("loop");
    assert(!audio.isPlaying());
    for (int i = 0; i < 8; ++i) assert(audio.playRaw(pcm(3, 20000), 255, "fx") != 0);
    assert(audio.playRaw(pcm(3, 1), 0, "ninth") == 0);
    out = audio.getPendingSamples();
    assert(std::count(out.begin(), out.end(), (s16)32767) == (int)out.size());
    audio.stopForMovie("fx");
    assert(!audio.isPlaying());
    audio.playRaw(pcm(4, -32768), 255, "negative");
    audio.setVolume(50);
    out = audio.getPendingSamples();
    assert(out[0] == -16384);
    record(out);
    audio.stopAll();
    size_t oldMusic = audio.playPcmStream(std::vector<float>(16, 0.25f), 2, 11025);
    size_t newMusic = audio.playPcmStream(std::vector<float>(16, 0.5f), 2, 11025);
    assert(!audio.isChannelPlaying(oldMusic) && audio.isChannelPlaying(newMusic));
    audio.appendPcmStream(std::vector<float>(16, -0.25f), 2, 11025, false);
    record(audio.getPendingSamples());
    assert(!audio.isChannelPlaying(newMusic));
    audio.appendPcmStream(std::vector<float>(16, 0.125f), 2, 11025, false);
    assert(audio.isPlaying());
    record(audio.getPendingSamples());
    audio.stopAll();
    audio.colorspace = ColorspaceArgb;
    out = audio.getPendingSamples();
    assert(out.size() == 735 * 2);
    assert(std::count(out.begin(), out.end(), (s16)0) == (int)out.size());
}

static void benchmark() {
    Native32Reader reader;
    SpriteSystem sprites;
    Renderer renderer(320, 240);
    std::vector<FrameObject> objects;
    for (int i = 0; i < 128; ++i) {
        std::string name = "long_sprite_override_name_for_benchmark_" + std::to_string(i);
        sprites.insert(name, MovieState(0, (i * 17) % 300, (i * 11) % 220, (i * 7) % 13));
        renderer.setSpriteOverride(name, solid(16, 16, 0xff000000u | i));
    }
    renderer.drawFrame(&reader, sprites, objects);
    allocations = 0;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3000; ++i) renderer.drawFrame(&reader, sprites, objects);
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("render_3000_us=%lld allocations=%zu\n", (long long)micros, allocations);
    record(renderer.buffer);

    AudioEngine audio(ColorspaceYuv, 100);
    audio.playRaw(pcm(11025, 100), 255, "music-like-loop");
    audio.getPendingSamples();
    allocations = 0;
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
        std::vector<s16> out = audio.getPendingSamples();
        assert(out[0] == 100);
    }
    micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("audio_10000_us=%lld allocations=%zu\n", (long long)micros, allocations);
}

static std::vector<s16> referenceResample(const std::vector<float>& samples,
                                        size_t channels, u32 inputRate, u32 outputRate) {
    std::vector<s16> out;
    if (samples.empty() || !channels || !inputRate || !outputRate) return out;
    size_t frames = samples.size() / channels;
    if (!frames) return out;
    size_t count = ((u64)frames * outputRate + inputRate - 1) / inputRate;
    for (size_t i = 0; i < count; ++i) {
        u64 pos = (u64)i * inputRate;
        size_t first = std::min((size_t)(pos / outputRate), frames - 1);
        size_t second = std::min(first + 1, frames - 1);
        float fraction = (float)(pos % outputRate) / (float)outputRate;
        for (size_t c = 0; c < 2; ++c) {
            size_t channel = std::min(c, channels - 1);
            float a = samples[first * channels + channel];
            float b = samples[second * channels + channel];
            s32 value = (s32)((a + (b - a) * fraction) * 32767.0f);
            out.push_back((s16)std::max(-32768, std::min(32767, value)));
        }
    }
    return out;
}

static void testResampling() {
    const u32 rates[] = {1, 8000, 11025, 22050, 32000, 44100, 48000, 96000,
                         0xffffffefu, 0xffffffffu};
    const size_t lengths[] = {0, 1, 2, 31, 1025};
    for (u32 in : rates) for (u32 out : rates) {
        if ((u64)out > (u64)in * 16) continue;
        for (size_t channels = 1; channels <= 3; ++channels) for (size_t n : lengths) {
            std::vector<float> samples(n * channels);
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] = ((int)((i * 971) % 4096) - 2048) / 1024.0f;
            std::vector<s16> actual = resampleToStereo(samples, channels, in, out);
            assert(actual == referenceResample(samples, channels, in, out));
            record(actual);
        }
    }
    std::vector<float> partial(5, 0.5f);
    assert(resampleToStereo(partial, 2, 48000, 44100) == referenceResample(partial, 2, 48000, 44100));
    assert(resampleToStereo(partial, 0, 48000, 44100).empty());
    assert(resampleToStereo(partial, 2, 0, 44100).empty());
    assert(resampleToStereo(partial, 2, 48000, 0).empty());
}

static mpeg::Frame videoFrame(size_t w, size_t h, size_t padding) {
    mpeg::Frame f;
    f.width = w;
    f.height = h;
    f.y.width = w + padding;
    f.y.height = h;
    f.cr.width = f.cb.width = (w + 1) / 2 + padding;
    f.cr.height = f.cb.height = (h + 1) / 2;
    f.y.data.resize(f.y.width * h);
    f.cr.data.resize(f.cr.width * f.cr.height);
    f.cb.data.resize(f.cb.width * f.cb.height);
    for (size_t i = 0; i < f.y.data.size(); ++i) f.y.data[i] = (i * 97 + 13) & 255;
    for (size_t i = 0; i < f.cr.data.size(); ++i) f.cr.data[i] = (i * 53) & 255;
    for (size_t i = 0; i < f.cb.data.size(); ++i) f.cb.data[i] = (i * 31 + 255) & 255;
    return f;
}

static int clipped(int v) { return std::max(0, std::min(255, v)); }

static std::vector<u32> referenceRgb(const mpeg::Frame& f, size_t w, size_t h) {
    std::vector<u32> result(w * h);
    for (size_t y = 0; y < h; ++y) for (size_t x = 0; x < w; ++x) {
        size_t sx = x * f.width / w, sy = y * f.height / h;
        int yy = ((int(f.y.data[sy * f.y.width + sx]) - 16) * 76309) >> 16;
        int cr = int(f.cr.data[sy / 2 * f.cr.width + sx / 2]) - 128;
        int cb = int(f.cb.data[sy / 2 * f.cb.width + sx / 2]) - 128;
        int r = clipped(yy + ((cr * 104597) >> 16));
        int g = clipped(yy - ((cb * 25674 + cr * 53278) >> 16));
        int b = clipped(yy + ((cb * 132201) >> 16));
        result[y * w + x] = 0xff000000u | (u32(r) << 16) | (u32(g) << 8) | u32(b);
    }
    return result;
}

static void testVideoConversion() {
    // Exercise clamping at chroma/luma extremes, including out-of-gamut RGB.
    // Keep this outside the historical digest so baseline comparisons remain useful.
    mpeg::Frame corner = videoFrame(1, 1, 0);
    const int levels[] = {0, 16, 128, 235, 255};
    for (int yy : levels) for (int cb = 0; cb <= 255; cb += 17)
        for (int cr = 0; cr <= 255; cr += 17) {
            corner.y.data[0] = (u8)yy;
            corner.cb.data[0] = (u8)cb;
            corner.cr.data[0] = (u8)cr;
            std::vector<u32> converted;
            corner.writeRgbScaled(&converted, 3, 2);
            assert(converted == referenceRgb(corner, 3, 2));
            corner.writeRgbScaled(&converted, 1, 1);
            assert(converted == referenceRgb(corner, 1, 1));
        }
    const size_t sizes[] = {1, 2, 3, 15, 16, 17, 33};
    for (size_t w : sizes) for (size_t h : sizes) for (size_t padding = 0; padding <= 3; padding += 3) {
        mpeg::Frame f = videoFrame(w, h, padding);
        for (int scale = 0; scale < 3; ++scale) {
            size_t dw = scale == 0 ? w : scale == 1 ? w * 2 + 1 : (w + 1) / 2;
            size_t dh = scale == 0 ? h : scale == 1 ? h * 2 + 1 : (h + 1) / 2;
            std::vector<u32> actual(dw * dh, 0xdeadbeef);
            f.writeRgbScaled(&actual, dw, dh);
            assert(actual == referenceRgb(f, dw, dh));
            record(actual);
        }
    }
#ifdef N32_REUSE_OUTPUT
    mpeg::Frame f = videoFrame(17, 15, 3);
    f.cb.width += 2;
    f.cb.data.resize(f.cb.width * f.cb.height, 127);
    std::vector<u32> actual;
    f.writeRgbScaled(&actual, 35, 31);
    assert(actual == referenceRgb(f, 35, 31));
    f.cr.data.resize(1);
    f.writeRgbScaled(&actual, 17, 15);
    assert(actual == std::vector<u32>(17 * 15, 0xff000000u));
    f.cr.width = 0;
    f.writeRgbScaled(&actual, 35, 31);
    assert(actual == std::vector<u32>(35 * 31, 0xff000000u));
#endif
}

static void nextAudio(AudioEngine& audio, std::vector<s16>* out) {
#ifdef N32_REUSE_OUTPUT
    audio.getPendingSamples(out);
#else
    *out = audio.getPendingSamples();
#endif
}

static void testReusableAudio() {
    AudioEngine reference(ColorspaceYuv, 100), reused(ColorspaceYuv, 100);
    reference.playRaw(pcm(3, 1234), 255, "loop");
    reused.playRaw(pcm(3, 1234), 255, "loop");
    std::vector<s16> actual(4096, -1);
    for (int tick = 0; tick < 120; ++tick) {
        if (tick == 30) reference.stopAll(), reused.stopAll();
        if (tick == 60) reference.startTone(), reused.startTone();
        if (tick == 90) reference.colorspace = reused.colorspace = ColorspaceArgb;
        nextAudio(reused, &actual);
        assert(actual == reference.getPendingSamples());
        record(actual);
    }
}

static void benchmarkRound2() {
    std::vector<float> samples(8192);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = (int(i % 97) - 48) / 48.0f;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
        std::vector<s16> out = resampleToStereo(samples, 2, 48000, 22050);
        digest ^= (u16)out[i % out.size()];
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("resample_500_us=%lld\n", (long long)us);
    AudioEngine audio(ColorspaceYuv, 100);
    audio.playRaw(pcm(11025, 100), 255, "loop");
    std::vector<s16> out;
    for (int i = 0; i < 4; ++i) nextAudio(audio, &out);
    allocations = 0;
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
        nextAudio(audio, &out);
        assert(out[0] == 100);
    }
    us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("reused_audio_10000_us=%lld allocations=%zu\n", (long long)us, allocations);
    mpeg::Frame frame = videoFrame(320, 240, 0);
    std::vector<u32> rgb;
    frame.writeRgbScaled(&rgb, 320, 240);
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) frame.writeRgbScaled(&rgb, 320, 240);
    us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("mpeg_rgb_500_us=%lld\n", (long long)us);
    record(rgb);
}

int main() {
    testClipping();
    testDrawOrder();
    testAudio();
    testResampling();
    testVideoConversion();
    testReusableAudio();
    benchmark();
    benchmarkRound2();
    std::printf("PASS digest=%016llx\n", digest);
}
