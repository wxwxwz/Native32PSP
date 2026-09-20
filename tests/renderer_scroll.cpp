#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Observe list size and cache access order without adding production test APIs.
#define private public
#include "core/renderer.h"
#undef private

using namespace n32;

struct Override {
    RgbaImage image;
    std::string leader;
    bool hasLeader;
};
typedef std::map<std::string, Override> Overrides;

static void put16(std::vector<u8>& bytes, size_t at, unsigned value) {
    bytes[at] = (u8)value;
    bytes[at + 1] = (u8)(value >> 8);
}

static void put32(std::vector<u8>& bytes, size_t at, unsigned value) {
    for (int i = 0; i < 4; ++i) bytes[at + i] = (u8)(value >> (8 * i));
}

static std::vector<u8> fixture(unsigned count) {
    // Last two images have invalid headers. Four movie slots follow the image
    // table, including an empty movie.
    std::vector<u8> bytes((count + 2 + 4) * 4, 0);
    for (unsigned id = 1; id <= count; ++id) {
        const unsigned width = 16 + id % 3, height = 15 + id % 5;
        const size_t start = bytes.size();
        put32(bytes, (id - 1) * 4, (unsigned)start);
        bytes.resize(start + 8, 0);
        put16(bytes, start, width); put16(bytes, start + 2, height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const bool visible = id % 17 && (id % 3 == 0 || (x + y + id) % 4);
                const size_t at = bytes.size();
                bytes.resize(at + (visible ? 4 : 2), 0);
                if (visible) {
                    put16(bytes, at, 0xc001);
                    put16(bytes, at + 2, 0x8000 | ((id * 997 + x * 31 + y) & 0x7fff));
                }
            }
        put32(bytes, start + 4, (unsigned)(bytes.size() - start - 8));
    }
    put32(bytes, count * 4, 0xffffffffu);
    const size_t bad = bytes.size();
    bytes.resize(bad + 8, 0); // Zero dimensions.
    put32(bytes, (count + 1) * 4, (unsigned)bad);
    const size_t movies = (count + 2) * 4;
    for (unsigned movie = 0; movie < 4; ++movie) {
        const size_t start = bytes.size();
        put32(bytes, movies + movie * 4, (unsigned)start);
        bytes.resize(start + 4 * 12, 0);
        if (movie == 2) continue;
        for (unsigned frame = 0; frame < 3; ++frame) {
            put16(bytes, start + frame * 12, 1 + movie * 3 + frame);
            put16(bytes, start + frame * 12 + 2, (unsigned)(int(frame * 11) - 9));
            put16(bytes, start + frame * 12 + 4, (unsigned)(7 - int(frame * 9)));
        }
    }
    return bytes;
}

static void initReader(Native32Reader* reader, const std::vector<u8>& data, unsigned count) {
    reader->setData(data);
    reader->colorspace = ColorspaceArgb;
    reader->movieIdx = (count + 2) * 4;
}

static RgbaImage image(unsigned width, unsigned height, u32 color) {
    RgbaImage out;
    out.width = width; out.height = height;
    out.pixels.assign((size_t)width * height, color);
    return out;
}

static void setOverride(Renderer* renderer, Overrides* overrides, const std::string& name,
                        const RgbaImage& pixels, const char* leader = 0) {
    Override value = {pixels, leader ? leader : "", leader != 0};
    (*overrides)[name] = value;
    if (leader) renderer->setSpriteOverride(name, pixels, leader);
    else renderer->setSpriteOverride(name, pixels);
}

struct ReferenceEntry {
    const RgbaImage* overrideImage;
    u32 index, depth;
    s64 x, y;
};

static bool overlap(s64 x, s64 y, u32 width, u32 height, u32 canvasW, u32 canvasH) {
    if (!width || !height || !canvasW || !canvasH || width > 0x7fffffffu ||
        height > 0x7fffffffu || canvasW > 0x7fffffffu || canvasH > 0x7fffffffu) return false;
    return std::max<s64>(x, 0) < std::min<s64>(x + width, canvasW) &&
           std::max<s64>(y, 0) < std::min<s64>(y + height, canvasH);
}

static void scalarBlit(std::vector<u32>* pixels, u32 canvasW, u32 canvasH,
                       const RgbaImage& source, s64 x, s64 y) {
    const size_t count = (size_t)std::min<u64>(source.pixels.size(),
                                               (u64)source.width * source.height);
    for (size_t i = 0; i < count; ++i) {
        const s64 tx = x + (s64)(i % source.width), ty = y + (s64)(i / source.width);
        if (tx < 0 || ty < 0 || tx >= canvasW || ty >= canvasH) continue;
        const size_t at = (size_t)ty * canvasW + (size_t)tx;
        if ((source.pixels[i] >> 24) && at < pixels->size()) (*pixels)[at] = source.pixels[i];
    }
}

// Original full collection/stable depth sort, followed by dimensions/decode in
// sorted order and an independent per-pixel compositor. No cached draw bounds.
static std::vector<u32> referenceFrame(Native32Reader* reader, const Renderer& renderer,
                                      const SpriteSystem& sprites,
                                      const std::vector<FrameObject>& objects,
                                      const Overrides& overrides, size_t* sorted) {
    std::vector<ReferenceEntry> entries;
    for (const FrameObject& object : objects)
        if (object.type == ObjectImage)
            entries.push_back({0, object.index, object.depth, object.x, object.y});
    for (const auto& pair : sprites.sprites) {
        const MovieState& state = pair.second;
        if (!state.visible) continue;
        const auto override = overrides.find(pair.first);
        if (override != overrides.end()) {
            if (override->second.hasLeader) {
                const MovieState* leader = sprites.get(override->second.leader);
                if (leader && !leader->visible) continue;
            }
            const auto* movie = reader->getMovieRef(state.movie);
            const MovieFrame* frame = movie && state.frame < movie->size() ? &(*movie)[state.frame] : 0;
            entries.push_back({&override->second.image, 0, state.depth,
                (s64)state.x + (frame ? frame->x : 0), (s64)state.y + (frame ? frame->y : 0)});
        } else {
            const auto* movie = reader->getMovieRef(state.movie);
            if (movie && state.frame < movie->size()) {
                const MovieFrame& frame = (*movie)[state.frame];
                entries.push_back({0, frame.image, state.depth,
                    (s64)state.x + frame.x, (s64)state.y + frame.y});
            }
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
        [](const ReferenceEntry& a, const ReferenceEntry& b) { return a.depth < b.depth; });
    *sorted = entries.size();
    std::vector<u32> out(renderer.buffer.size(), 0xff000000u);
    for (const ReferenceEntry& entry : entries) {
        const s64 x = entry.x + renderer.screenX, y = entry.y + renderer.screenY;
        const RgbaImage* source = entry.overrideImage;
        if (!source) {
            u32 width, height;
            if (!reader->getImageDimensions(entry.index, &width, &height) ||
                !overlap(x, y, width, height, renderer.width, renderer.height)) continue;
            source = reader->getImageRef(entry.index);
        } else if (!overlap(x, y, source->width, source->height, renderer.width, renderer.height)) continue;
        if (source) scalarBlit(&out, renderer.width, renderer.height, *source, x, y);
    }
    return out;
}

static unsigned long long hash = 1469598103934665603ull;
static void compare(Native32Reader* reader, Native32Reader* reference, Renderer* renderer,
                     const SpriteSystem& sprites, const std::vector<FrameObject>& objects,
                     const Overrides& overrides, size_t* sorted = 0) {
    size_t fullSorted = 0;
    const std::vector<u32> expected = referenceFrame(reference, *renderer, sprites, objects, overrides, &fullSorted);
    assert(renderer->buffer == expected);
    assert(reader->imageCacheBytes() == reference->imageCacheBytes());
    assert(reader->imageCacheCount() == reference->imageCacheCount());
    assert(reader->imageCacheEvictions() == reference->imageCacheEvictions());
    assert(reader->imageLastUsed == reference->imageLastUsed);
    assert(reader->imageClock == reference->imageClock);
    assert(reader->imageValidCache == reference->imageValidCache);
    if (sorted) *sorted = fullSorted;
    for (u32 pixel : renderer->buffer) hash = (hash ^ pixel) * 1099511628211ull;
}

static void testEdgesAndChanges() {
    Native32Reader reader, reference;
    const auto data = fixture(12);
    initReader(&reader, data, 12); initReader(&reference, data, 12);
    Renderer renderer(31, 23);
    SpriteSystem sprites;
    Overrides overrides;
    std::vector<FrameObject> objects(7);
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i].index = (u16)(i + 1);
        objects[i].x = (s16)(int(i) * 11 - 21);
        objects[i].y = (s16)(int(i) * 7 - 12);
        objects[i].depth = 5;
    }
    objects[4].index = 13; objects[5].index = 14; objects[6].index = 0;
    sprites.insert("a", MovieState(1, 3, 4, 5));
    sprites.insert("b", MovieState(2, -2, 1, 5));
    sprites.insert("leader", MovieState(3, 0, 0, 1));
    sprites.insert("plain", MovieState(1, 7, 9, 6));
    setOverride(&renderer, &overrides, "a", image(41, 37, 0x01010203), "leader");
    setOverride(&renderer, &overrides, "b", image(13, 11, 0xffaabbcc), "missing");
    for (int step = 0; step < 90; ++step) {
        renderer.screenX = step % 15 - 7;
        renderer.screenY = step % 13 - 6;
        sprites.getMutable("a")->frame = step % 5; // Includes missing movie frames.
        sprites.getMutable("b")->frame = (step / 3) % 3;
        if (MovieState* plain = sprites.getMutable("plain")) plain->frame = (step / 2) % 3;
        sprites.getMutable("leader")->visible = step % 7 != 0;
        sprites.getMutable("b")->visible = step % 5 != 0;
        sprites.getMutable("a")->depth = step % 2 ? 5 : 7;
        objects[0].x = (s16)(step % 61 - 30);
        if (step == 18) {
            RgbaImage sparse = image(41, 37, 0);
            sparse.pixels[40] = 0x01020304; sparse.pixels[41 * 36] = 0xff987654;
            setOverride(&renderer, &overrides, "a", sparse, "leader");
        }
        if (step == 31) {
            RgbaImage truncated = image(13, 11, 0xff123456);
            truncated.pixels.resize(17);
            setOverride(&renderer, &overrides, "b", truncated);
        }
        if (step == 47) {
            sprites.sprites.erase("plain");
            sprites.insert("new", MovieState(2, 1, -3, 5));
        }
        if (step == 60) { renderer.clearSpriteOverrides(); overrides.clear(); }
        renderer.drawFrame(&reader, sprites, objects);
        compare(&reader, &reference, &renderer, sprites, objects, overrides);
    }
    for (s32 x : {std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max(), 0})
        for (s32 y : {std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max(), 0}) {
            renderer.screenX = x; renderer.screenY = y;
            objects[0].x = std::numeric_limits<s16>::min();
            objects[1].x = std::numeric_limits<s16>::max();
            sprites.getMutable("a")->x = std::numeric_limits<s16>::min();
            sprites.getMutable("b")->y = std::numeric_limits<s16>::max();
            renderer.drawFrame(&reader, sprites, objects);
            compare(&reader, &reference, &renderer, sprites, objects, overrides);
        }
    sprites.clear();
    objects.resize(3);
    objects[0].index = 0; objects[1].index = 13; objects[2].index = 14;
    renderer.screenX = renderer.screenY = 0;
    renderer.drawFrame(&reader, sprites, objects);
    compare(&reader, &reference, &renderer, sprites, objects, overrides);
#ifdef N32_EARLY_CULL
    assert(renderer.drawList.empty());
#endif
}

static void testScroll() {
    const unsigned count = 600;
    Native32Reader reader, reference;
    const auto data = fixture(count);
    initReader(&reader, data, count); initReader(&reference, data, count);
    Renderer renderer(320, 240);
    SpriteSystem sprites;
    Overrides overrides;
    std::vector<FrameObject> objects(4096);
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i].index = (u16)(i % count + 1);
        objects[i].x = (s16)((i % 64) * 24);
        objects[i].y = (s16)((i / 64) * 24);
        objects[i].depth = (u16)((i * 37) % 11);
    }
    sprites.insert("moving", MovieState(1, 80, 60, 20));
    sprites.insert("overlay", MovieState(3, 0, 0, 30));
    RgbaImage overlay = image(97, 71, 0);
    for (unsigned y = 0; y < 71; ++y)
        for (unsigned x = 0; x < 97; ++x)
            if ((x + 3 * y) % 5) overlay.pixels[y * 97 + x] = 0x01102030;
    setOverride(&renderer, &overrides, "overlay", overlay);
    long long elapsedNs = 0;
    size_t sortedTotal = 0, fullTotal = 0, largestList = 0;
    const unsigned frames = 192;
    for (unsigned frame = 0; frame < frames; ++frame) {
        renderer.screenX = -(s32)((frame * 29) % 1200);
        renderer.screenY = -(s32)((frame * 17) % 1200);
        MovieState* moving = sprites.getMutable("moving");
        moving->x = (s16)(100 - renderer.screenX + frame % 37);
        moving->y = (s16)(90 - renderer.screenY + frame % 19);
        moving->frame = frame % 3;
        sprites.getMutable("overlay")->x = (s16)(9 - renderer.screenX);
        sprites.getMutable("overlay")->y = (s16)(7 - renderer.screenY);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        renderer.drawFrame(&reader, sprites, objects);
        elapsedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        size_t fullSorted;
        compare(&reader, &reference, &renderer, sprites, objects, overrides, &fullSorted);
        sortedTotal += renderer.drawList.size(); fullTotal += fullSorted;
        largestList = std::max(largestList, renderer.drawList.size());
#ifdef N32_EARLY_CULL
        assert(renderer.drawList.size() <= 170);
#endif
    }
    assert(reader.imageCacheEvictions() > 0); // Scroll exceeds the 512-image entry cap.
    std::printf("scroll: frames=%u objects=%zu sort_avg=%zu full_sort_avg=%zu sort_max=%zu "
                "draw_us=%lld cache=%zu evictions=%llu hash=%016llx\n", frames, objects.size(),
                sortedTotal / frames, fullTotal / frames, largestList, elapsedNs / 1000,
                reader.imageCacheCount(), (unsigned long long)reader.imageCacheEvictions(), hash);
}

int main() {
    testEdgesAndChanges();
    testScroll();
    std::puts("PASS: full-sort scalar pixels, scrolling tiles, fresh decode/eviction order, "
              "equal depth, overrides, leaders, sprite edits, invalid images and extreme offsets");
}
