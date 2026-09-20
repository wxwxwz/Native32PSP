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

static void testCachedImageVisibility() {
    // One valid 7x5 ARGB1555 white image. Drawing it offscreen must not
    // populate the decoded-image cache; entering the screen must still work.
    Native32Reader reader;
    reader.colorspace = ColorspaceArgb;
    reader.data.assign(16, 0);
    reader.data[0] = 4;
    reader.data[4] = 7;
    reader.data[6] = 5;
    reader.data[8] = 4;
    reader.data[12] = 35;
    reader.data[13] = 0xc0;
    reader.data[14] = reader.data[15] = 0xff;
    Renderer renderer(4, 4);
    SpriteSystem sprites;
    std::vector<FrameObject> objects(1);
    objects[0].type = ObjectImage;
    objects[0].index = 1;
    objects[0].depth = 0;
    const int outside[][2] = {{4, 0}, {-7, 0}, {0, 4}, {0, -5}};
    for (const auto& position : outside) {
        objects[0].x = position[0];
        objects[0].y = position[1];
        renderer.drawFrame(&reader, sprites, objects);
        assert(reader.imageCacheCount() == 0);
        assert(reader.imageCacheBytes() == 0);
        assert(renderer.buffer == std::vector<u32>(16, 0xff000000));
    }
    // Exercise the cached opaque path at every edge against a scalar result.
    for (int y = -5; y <= 4; ++y) for (int x = -7; x <= 4; ++x) {
        objects[0].x = x;
        objects[0].y = y;
        renderer.drawFrame(&reader, sprites, objects);
        std::vector<u32> expected(16, 0xff000000);
        for (int sy = 0; sy < 5; ++sy) for (int sx = 0; sx < 7; ++sx) {
            int dx = x + sx, dy = y + sy;
            if (dx >= 0 && dx < 4 && dy >= 0 && dy < 4) expected[dy * 4 + dx] = 0xfff8f8f8;
        }
        assert(renderer.buffer == expected);
    }
    assert(reader.imageCacheCount() == 1 && reader.imageCacheBytes() == 7 * 5 * sizeof(u32));
    assert(reader.imageCacheEvictions() == 0);

    // Script offsets must not overflow before rejecting a far-away image.
    objects[0].x = 100;
    objects[0].y = 0;
    renderer.screenX = 0x7fffffff;
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer == std::vector<u32>(16, 0xff000000));
    objects[0].x = -100;
    renderer.screenX = (-0x7fffffff - 1);
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer == std::vector<u32>(16, 0xff000000));
}

static void testOverridePixelCopies() {
    Native32Reader reader;
    SpriteSystem sprites;
    sprites.insert("image", MovieState());
    std::vector<FrameObject> objects;
    Renderer renderer(5, 4);
    for (int variant = 0; variant < 3; ++variant) {
        RgbaImage image = solid(7, 5, 0);
        for (size_t i = 0; i < image.pixels.size(); ++i)
            image.pixels[i] = ((variant == 1 && i % 3 == 0) ? 0 : 0x01000000u) | (u32)i;
        // Truncated opaque images must still obey their actual source length.
        if (variant == 2) image.pixels.resize(13);
        renderer.setSpriteOverride("image", image);
        for (int y = -5; y <= 4; ++y) for (int x = -7; x <= 5; ++x) {
            sprites.getMutable("image")->x = x;
            sprites.getMutable("image")->y = y;
            std::vector<u32> expected(20, 0xff000000);
            for (size_t i = 0; i < image.pixels.size(); ++i) {
                int dx = x + (int)(i % image.width), dy = y + (int)(i / image.width);
                if (dx >= 0 && dx < 5 && dy >= 0 && dy < 4 && image.pixels[i] >> 24)
                    expected[dy * 5 + dx] = image.pixels[i];
            }
            renderer.drawFrame(&reader, sprites, objects);
            assert(renderer.buffer == expected);
        }
    }
    // Mutating the caller's image must not invalidate an override's owned copy.
    RgbaImage owned = solid(1, 1, 0x01112233);
    sprites.getMutable("image")->x = sprites.getMutable("image")->y = 0;
    renderer.setSpriteOverride("image", owned);
    owned.pixels[0] = 0;
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer[0] == 0x01112233);
    // The public blitter accepts mutable images and must not retain opacity.
    renderer.blitImage(owned, 0, 0);
    assert(renderer.buffer[0] == 0x01112233);
}

static void referenceBlit(std::vector<u32>& pixels, u32 width, u32 height,
                          const RgbaImage& image, s64 x, s64 y) {
    if (!image.width) return;
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        if (i / image.width >= image.height) break;
        const s64 dx = x + (s64)(i % image.width), dy = y + (s64)(i / image.width);
        if (dx >= 0 && dx < width && dy >= 0 && dy < height && (image.pixels[i] >> 24))
            pixels[(size_t)dy * width + (size_t)dx] = image.pixels[i];
    }
}

static RgbaImage sparseImage(u32 width, u32 height, bool holes) {
    RgbaImage image = solid(width, height, 0x00abcdef);
    for (u32 y = height / 3; y < height * 2 / 3; ++y)
        for (u32 x = width / 3; x < width * 2 / 3; ++x)
            if (!holes || (x + y) % 3)
                image.pixels[(size_t)y * width + x] = 0x01000000u | (y << 12) | (x << 4) | 7;
    return image;
}

static void testTransparentBounds() {
    Native32Reader reader;
    Renderer renderer(11, 9);
    SpriteSystem sprites;
    sprites.insert("image", MovieState());
    const std::vector<FrameObject> objects;
    const int positions[] = {-17, -13, -12, -7, -1, 0, 1, 7, 10, 11, 17};
    for (unsigned variant = 0; variant < 6; ++variant) {
        RgbaImage image = variant < 2 ? sparseImage(13, 11, variant == 1) : solid(13, 11, 0x01112233);
        if (variant == 2) image.pixels.resize(17); // Opaque prefix, missing rows.
        if (variant == 3) image = solid(13, 11, 0x00abcdef); // Transparent RGB isn't visible.
        if (variant == 4) {
            image = solid(13, 11, 0);
            image.pixels[1] = 0x01020304;
            image.pixels[13 * 9 + 11] = 0x01808080;
        }
        if (variant == 5) image.pixels.push_back(0x00010203); // Ignore storage past the dimensions.
        renderer.setSpriteOverride("image", image);
        for (int y : positions) for (int x : positions) {
            sprites.getMutable("image")->x = x;
            sprites.getMutable("image")->y = y;
            // An earlier frame must not leak when the visible box disappears.
            std::fill(renderer.buffer.begin(), renderer.buffer.end(), 0xffadbeefu);
            std::vector<u32> expected(11 * 9, 0xff000000);
            referenceBlit(expected, 11, 9, image, x, y);
            renderer.drawFrame(&reader, sprites, objects);
            assert(renderer.buffer == expected);
        }
    }
    for (s32 extreme : {s32(0x7fffffff), s32(-0x7fffffff - 1)}) {
        sprites.getMutable("image")->x = extreme;
        sprites.getMutable("image")->y = extreme;
        renderer.screenX = renderer.screenY = extreme;
        renderer.drawFrame(&reader, sprites, objects);
        assert(renderer.buffer == std::vector<u32>(11 * 9, 0xff000000));
    }
}

static void testBackgroundInitialization() {
    Native32Reader reader;
    Renderer renderer(11, 9);
    SpriteSystem sprites;
    sprites.insert("background", MovieState(0, 0, 0, 0));
    sprites.insert("sprite", MovieState(0, 2, 1, 1));
    RgbaImage foreground = sparseImage(7, 5, true);
    renderer.setSpriteOverride("sprite", foreground);
    const std::vector<FrameObject> objects;
    for (int variant = 0; variant < 7; ++variant) {
        RgbaImage background = variant == 0 ? solid(13, 11, 0x01123456) : solid(11, 9, 0x01123456);
        sprites.getMutable("background")->x = sprites.getMutable("background")->y = variant == 0 ? -1 : 0;
        sprites.getMutable("background")->visible = variant != 6;
        if (variant == 2) background.pixels.resize(15);
        if (variant == 3) background.pixels[0] = 0x00123456;
        if (variant == 4) background = solid(11, 9, 0x00123456);
        if (variant == 5) sprites.getMutable("background")->x = 11;
        renderer.setSpriteOverride("background", background);
        std::vector<u32> expected(11 * 9, 0xff000000);
        if (variant != 6) referenceBlit(expected, 11, 9, background,
            sprites.get("background")->x, sprites.get("background")->y);
        referenceBlit(expected, 11, 9, foreground, 2, 1);
        std::fill(renderer.buffer.begin(), renderer.buffer.end(), 0xfffedcba);
        renderer.drawFrame(&reader, sprites, objects);
        assert(renderer.buffer == expected);
    }
    renderer.clearSpriteOverrides();
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer == std::vector<u32>(11 * 9, 0xff000000));
    renderer.setSpriteOverride("background", solid(11, 9, 0x01123456));
    sprites.getMutable("background")->visible = true;
    sprites.getMutable("background")->x = sprites.getMutable("background")->y = 0;
    renderer.buffer.push_back(0xfffedcba);
    renderer.drawFrame(&reader, sprites, objects);
    assert(renderer.buffer.back() == 0xff000000); // Public oversized destination tail.
    renderer.drawFrame(0, sprites, objects);
    assert(renderer.buffer == std::vector<u32>(11 * 9 + 1, 0xff000000));
}

static std::vector<u8> transparentCacheFixture(unsigned count) {
    std::vector<u8> data(count * 4, 0);
    for (unsigned id = 0; id < count; ++id) {
        const size_t start = data.size();
        for (int byte = 0; byte < 4; ++byte) data[id * 4 + byte] = (u8)(start >> (byte * 8));
        data.resize(start + 8, 0);
        data[start] = 13; data[start + 2] = 11;
        for (unsigned y = 0; y < 11; ++y) for (unsigned x = 0; x < 13; ++x) {
            const bool visible = x >= 3 && x < 9 && y >= 2 && y < 8 && (x + y + id) % 3;
            data.push_back(visible ? 1 : 0); data.push_back(visible ? 0xc0 : 0);
            if (visible) { data.push_back(0xff); data.push_back(0xff); }
        }
        size_t bytes = data.size() - start - 8;
        for (int byte = 0; byte < 4; ++byte) data[start + 4 + byte] = (u8)(bytes >> (byte * 8));
    }
    return data;
}

static void testBoundsAfterEviction() {
    Native32Reader reader;
    reader.setData(transparentCacheFixture(514));
    reader.colorspace = ColorspaceArgb;
    Renderer renderer(11, 9);
    SpriteSystem sprites;
    std::vector<FrameObject> objects(1);
    objects[0].index = 1;
    RgbaImage expectedImage = solid(13, 11, 0);
    for (unsigned y = 2; y < 8; ++y) for (unsigned x = 3; x < 9; ++x)
        if ((x + y) % 3) expectedImage.pixels[y * 13 + x] = 0xfff8f8f8;
    for (int pass = 0; pass < 2; ++pass) {
        for (int x = -13; x <= 11; ++x) {
            objects[0].x = x; objects[0].y = -2;
            std::vector<u32> expected(11 * 9, 0xff000000);
            referenceBlit(expected, 11, 9, expectedImage, x, -2);
            renderer.drawFrame(&reader, sprites, objects);
            assert(renderer.buffer == expected);
        }
        if (pass == 0) {
            for (unsigned id = 2; id <= 514; ++id) assert(reader.getImageRef(id));
            assert(reader.imageCacheCount() == 512 && reader.imageCacheEvictions() >= 2);
        }
    }
    assert(reader.imageCacheEvictions() >= 3); // The visible first image was decoded again.
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

static void benchmarkMovingTransparency() {
    for (int mixed = 0; mixed < 2; ++mixed) {
        Native32Reader reader;
        SpriteSystem sprites;
        Renderer renderer(320, 240);
        const std::vector<FrameObject> objects;
        sprites.insert("background", MovieState(0, 0, 0, 0));
        renderer.setSpriteOverride("background", solid(320, 240, 0xff183048));
        std::vector<MovieState*> moving;
        for (unsigned i = 0; i < 32; ++i) {
            const std::string name = "sprite_" + std::to_string(i);
            sprites.insert(name, MovieState(0, 0, 0, i + 1));
            RgbaImage image = sparseImage(96, 96, mixed != 0);
            if (mixed && i % 4 == 2) image = solid(24, 24, 0x01102030u + i);
            if (mixed && i % 4 == 3) {
                image = solid(48, 48, 0x00abcdef);
                for (size_t pixel = 0; pixel < image.pixels.size(); ++pixel)
                    if ((pixel + pixel / 48) % 3) image.pixels[pixel] = 0x01010203u + i;
            }
            renderer.setSpriteOverride(name, image);
            moving.push_back(sprites.getMutable(name));
        }
        renderer.drawFrame(&reader, sprites, objects);
        allocations = 0;
        unsigned long long frameHash = 1469598103934665603ull;
        long long nanoseconds = 0;
        for (unsigned frame = 0; frame < 1500; ++frame) {
            for (unsigned i = 0; i < moving.size(); ++i) {
                moving[i]->x = (s32)((frame * 7 + i * 31) % 432) - 80;
                moving[i]->y = (s32)((frame * 3 + i * 19) % 336) - 48;
            }
            const auto begin = std::chrono::steady_clock::now();
            renderer.drawFrame(&reader, sprites, objects);
            nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count();
            // Hash every pixel of every moving frame, outside the timed region.
            for (u32 pixel : renderer.buffer) frameHash = (frameHash ^ pixel) * 1099511628211ull;
        }
        std::printf("render_%s_moving_1500_us=%lld allocations=%zu digest=%016llx\n",
            mixed ? "mixed" : "sparse", nanoseconds / 1000, allocations, frameHash);
    }
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
    testCachedImageVisibility();
    testOverridePixelCopies();
    testTransparentBounds();
    testBackgroundInitialization();
    testBoundsAfterEviction();
    testAudio();
    testResampling();
    testVideoConversion();
    testReusableAudio();
    benchmark();
    benchmarkMovingTransparency();
    benchmarkRound2();
    std::printf("PASS digest=%016llx\n", digest);
}
