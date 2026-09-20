#include "core/image_decoder.h"
#include "reference_image_decoder.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// This executable is an ordinary host benchmark, not a sanitizer target.
// Count only new/new[] calls between the start and end of each timed batch.
static size_t allocations;
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

using namespace n32;

namespace {

struct Asset {
    const char* name;
    bool yuv;
    size_t offset;
    size_t length;
    unsigned iterations;
    std::vector<u8> file;
};

void append16(std::vector<u8>* bytes, u16 value) {
    bytes->push_back((u8)value);
    bytes->push_back((u8)(value >> 8));
}

void store32(std::vector<u8>* bytes, size_t offset, u32 value) {
    for (size_t i = 0; i < 4; ++i) (*bytes)[offset + i] = (u8)(value >> (i * 8));
}

Asset makeAsset(const char* name, bool yuv, u16 width, u16 height,
                const std::vector<u8>& payload, unsigned iterations) {
    Asset asset;
    asset.name = name;
    asset.yuv = yuv;
    asset.offset = 13; // A borrowed image need not be naturally aligned.
    asset.length = 8 + payload.size();
    asset.iterations = iterations;
    asset.file.assign(asset.offset, 0xa5);
    append16(&asset.file, width);
    append16(&asset.file, height);
    asset.file.resize(asset.offset + 8);
    store32(&asset.file, asset.offset + 4, (u32)payload.size());
    asset.file.insert(asset.file.end(), payload.begin(), payload.end());
    asset.file.insert(asset.file.end(), 16, 0x5a);
    return asset;
}

void appendYuvSample(std::vector<u8>* payload, size_t block, bool mixed) {
    for (size_t pixel = 0; pixel < 4; ++pixel) {
        const size_t position = block * 4 + pixel;
        payload->push_back(mixed && position % 5 == 0 ? 0 :
                           (u8)(16 + (position * 37 + block / 11) % 220));
    }
    // Include zeros to exercise the original neighbor-substitution rule.
    payload->push_back(block % 7 == 0 ? 0 : (u8)(32 + block * 13 % 192));
    payload->push_back(block % 11 == 0 ? 0 : (u8)(32 + block * 23 % 192));
}

Asset makeYuv(const char* name, u16 width, u16 height, bool mixed, unsigned iterations) {
    const size_t total = ((width + 1) / 2) * ((height + 1) / 2);
    std::vector<u8> payload;
    size_t block = 0, packet = 0;
    while (block < total) {
        const bool literal = packet++ % 3 != 0;
        const size_t count = std::min(total - block, (size_t)(literal ? 53 : 17));
        append16(&payload, (u16)(count | (literal ? 0x8000 : 0)));
        for (size_t i = 0; i < (literal ? count : 1); ++i)
            appendYuvSample(&payload, block + i, mixed);
        block += count;
    }
    return makeAsset(name, true, width, height, payload, iterations);
}

Asset makeArgb() {
    const size_t total = 320 * 240;
    std::vector<u8> payload;
    size_t pixel = 0, packet = 0;
    while (pixel < total) {
        if (packet % 9 == 0) {
            append16(&payload, 0);
            ++pixel;
        } else {
            const size_t count = std::min(total - pixel, (size_t)(1 + packet * 29 % 129));
            append16(&payload, (u16)(0xc000 | count));
            const u16 color = (u16)((packet * 997) & 0x7fff);
            append16(&payload, packet % 5 == 0 ? color : (u16)(0x8000 | color));
            pixel += count;
        }
        ++packet;
    }
    return makeAsset("argb_320x240_rle", false, 320, 240, payload, 512);
}

// Keep wrappers out of the timed loop's optimizer view. Both variants produce
// a fresh output, and release all scratch storage, on every invocation.
__attribute__((noinline)) bool decodeBefore(const Asset& asset, RgbaImage* image,
                                           ImageDrawInfo* info) {
    std::vector<u8> copy(asset.file.begin() + asset.offset,
                         asset.file.begin() + asset.offset + asset.length);
    // Compare the old full-plane buffers with the new row-buffer organization
    // under the same residual-zero chroma rule. The oracle's default still
    // preserves the old colors for historical correctness comparisons.
    const bool ok = asset.yuv ? n32_image_reference::decodeImageYuv(copy, image, true)
                              : n32_image_reference::decodeImageArgb(copy, image);
    if (ok) *info = imageDrawInfo(*image);
    return ok;
}

__attribute__((noinline)) bool decodeAfter(const Asset& asset, RgbaImage* image,
                                          ImageDrawInfo* info) {
    const u8* data = &asset.file[asset.offset];
    return asset.yuv ? decodeImageYuv(data, asset.length, image, info)
                     : decodeImageArgb(data, asset.length, image, info);
}

bool sameInfo(const ImageDrawInfo& a, const ImageDrawInfo& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom && a.allPixelsVisible == b.allPixelsVisible;
}

void record(unsigned long long* digest, u32 value) {
    *digest ^= value;
    *digest *= 1099511628211ull;
}

unsigned long long validate(const Asset& asset) {
    RgbaImage before = {}, after = {};
    ImageDrawInfo beforeInfo, afterInfo;
    if (!decodeBefore(asset, &before, &beforeInfo) || !decodeAfter(asset, &after, &afterInfo) ||
        before.width != after.width || before.height != after.height ||
        before.pixels != after.pixels || !sameInfo(beforeInfo, afterInfo)) {
        std::fprintf(stderr, "%s: old/new pixels or metadata differ\n", asset.name);
        std::exit(1);
    }
    unsigned long long digest = 1469598103934665603ull;
    record(&digest, before.width);
    record(&digest, before.height);
    for (size_t i = 0; i < before.pixels.size(); ++i) record(&digest, before.pixels[i]);
    record(&digest, beforeInfo.left);
    record(&digest, beforeInfo.top);
    record(&digest, beforeInfo.right);
    record(&digest, beforeInfo.bottom);
    record(&digest, beforeInfo.allPixelsVisible ? 1 : 0);
    return digest;
}

typedef bool (*DecodePath)(const Asset&, RgbaImage*, ImageDrawInfo*);

void runBatch(const Asset& asset, DecodePath decode, unsigned iterations) {
    for (unsigned i = 0; i < iterations; ++i) {
        RgbaImage image = {};
        ImageDrawInfo info;
        if (!decode(asset, &image, &info)) {
            std::fprintf(stderr, "%s: decode failed during benchmark\n", asset.name);
            std::exit(1);
        }
    }
}

void benchmark(const Asset& asset, DecodePath decode, const char* variant,
               unsigned long long digest) {
    runBatch(asset, decode, 8);
    allocations = 0;
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    runBatch(asset, decode, asset.iterations);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const size_t measuredAllocations = allocations;
    const long long micros = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    std::printf("decode_%s_%s iterations=%u total_us=%lld allocations=%zu digest=%016llx\n",
                variant, asset.name, asset.iterations, micros, measuredAllocations, digest);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "--before") && std::strcmp(argv[1], "--after"))) {
        std::fprintf(stderr, "usage: %s --before|--after\n", argv[0]);
        return 2;
    }
    const bool before = std::strcmp(argv[1], "--before") == 0;
    const DecodePath decode = before ? decodeBefore : decodeAfter;
    const char* variant = before ? "before" : "after";
    const Asset assets[] = {
        makeYuv("yuv_320x240_opaque", 320, 240, false, 256),
        makeYuv("yuv_320x240_mixed", 320, 240, true, 256),
        makeYuv("yuv_64x64_sprite", 64, 64, true, 4096),
        makeArgb()
    };
    const size_t count = sizeof(assets) / sizeof(assets[0]);
    unsigned long long digests[count];
    for (size_t i = 0; i < count; ++i) digests[i] = validate(assets[i]);
    std::printf("image-decode-bench variant=%s correctness=old-new-pixels-and-metadata-equal\n", variant);
    for (size_t i = 0; i < count; ++i) benchmark(assets[i], decode, variant, digests[i]);
    return 0;
}
