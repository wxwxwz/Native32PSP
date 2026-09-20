#include "core/image_decoder.h"
#include "core/native32_reader.h"
#include "reference_image_decoder.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace n32;

namespace {

unsigned checks = 0;
u32 randomState = 0x317902abu;

u32 randomValue() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

void require(bool condition, const char* label, const char* detail) {
    if (!condition) {
        std::fprintf(stderr, "image decode case %u (%s): %s\n", checks, label, detail);
        std::abort();
    }
}

void append16(std::vector<u8>* bytes, u16 value) {
    bytes->push_back((u8)value);
    bytes->push_back((u8)(value >> 8));
}

void store32(std::vector<u8>* bytes, size_t offset, u32 value) {
    for (size_t i = 0; i < 4; ++i) (*bytes)[offset + i] = (u8)(value >> (8 * i));
}

std::vector<u8> imageBlock(u16 width, u16 height, const std::vector<u8>& payload) {
    std::vector<u8> block;
    append16(&block, width);
    append16(&block, height);
    block.resize(8);
    store32(&block, 4, (u32)payload.size());
    block.insert(block.end(), payload.begin(), payload.end());
    return block;
}

bool sameImage(const RgbaImage& a, const RgbaImage& b) {
    return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}

bool sameInfo(const ImageDrawInfo& a, const ImageDrawInfo& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom && a.allPixelsVisible == b.allPixelsVisible;
}

bool decodeSpan(bool yuv, const u8* data, size_t size, RgbaImage* image,
                ImageDrawInfo* info = 0) {
    return yuv ? decodeImageYuv(data, size, image, info)
               : decodeImageArgb(data, size, image, info);
}

bool decodeVector(bool yuv, const std::vector<u8>& data, RgbaImage* image) {
    return yuv ? decodeImageYuv(data, image) : decodeImageArgb(data, image);
}

bool referenceDecode(bool yuv, const std::vector<u8>& data, RgbaImage* image) {
    return yuv ? n32_image_reference::decodeImageYuv(data, image, true)
               : n32_image_reference::decodeImageArgb(data, image);
}

void checkBlock(bool yuv, const std::vector<u8>& bytes, const char* label) {
    ++checks;
    RgbaImage reference = {}, vectorImage = {}, spanImage = {}, noInfoImage = {};
    const bool expected = referenceDecode(yuv, bytes, &reference);
    require(decodeVector(yuv, bytes, &vectorImage) == expected, label, "vector status");

    // Nonzero prefix forces an unaligned span. Suffix must not influence a
    // truncated payload; its bytes deliberately resemble valid operations.
    std::vector<u8> guarded(1, 0xa5);
    guarded.insert(guarded.end(), bytes.begin(), bytes.end());
    guarded.insert(guarded.end(), 32, 0xff);
    ImageDrawInfo info;
    info.left = 101; info.top = 102; info.right = 103; info.bottom = 104;
    info.allPixelsVisible = true;
    require(decodeSpan(yuv, &guarded[1], bytes.size(), &spanImage, &info) == expected,
            label, "span status");
    require(decodeSpan(yuv, &guarded[1], bytes.size(), &noInfoImage) == expected,
            label, "optional metadata status");
    if (expected) {
        require(sameImage(reference, vectorImage), label, "vector pixels");
        require(sameImage(reference, spanImage), label, "span pixels");
        require(sameImage(reference, noInfoImage), label, "pixels without metadata");
        require(sameInfo(info, imageDrawInfo(reference)), label, "fused metadata");
    }
    require(!decodeSpan(yuv, &guarded[1], bytes.size(), 0, &info), label, "null output");
}

u8 sampleByte() {
    const u32 value = randomValue();
    return value % 4 == 0 ? 0 : (u8)(value >> 8);
}

void appendYuvSample(std::vector<u8>* payload) {
    for (size_t i = 0; i < 6; ++i) payload->push_back(sampleByte());
}

std::vector<u8> generatedYuv(u16 width, u16 height) {
    std::vector<u8> payload;
    const size_t total = ((width + 1) / 2) * ((height + 1) / 2);
    size_t done = 0;
    while (done < total) {
        const size_t count = std::min(total - done, (size_t)(1 + randomValue() % 11));
        const bool literal = (randomValue() & 1) != 0;
        append16(&payload, (u16)(count | (literal ? 0x8000 : 0)));
        for (size_t i = 0; i < (literal ? count : 1); ++i) appendYuvSample(&payload);
        done += count;
    }
    return imageBlock(width, height, payload);
}

std::vector<u8> generatedArgb(u16 width, u16 height) {
    std::vector<u8> payload;
    const size_t total = (size_t)width * height;
    size_t done = 0;
    while (done < total) {
        if (randomValue() % 4 == 0) {
            append16(&payload, 0);
            ++done;
        } else {
            const size_t count = std::min(total - done, (size_t)(1 + randomValue() % 19));
            append16(&payload, (u16)(0xc000 | count));
            append16(&payload, (u16)randomValue());
            done += count;
        }
    }
    return imageBlock(width, height, payload);
}

void checkInterpolationOrder() {
    // At the central zero, Y-first selects 90/180 from above/below; X-first
    // would select 40/130. This makes changing interpolation order observable.
    const u8 chroma[9] = { 0, 90, 0, 40, 0, 130, 0, 180, 0 };
    std::vector<u8> payload;
    append16(&payload, 0x8009);
    for (size_t i = 0; i < 9; ++i) {
        payload.insert(payload.end(), 4, 96);
        payload.push_back(chroma[i]);
        payload.push_back(128);
    }
    const std::vector<u8> block = imageBlock(6, 6, payload);
    checkBlock(true, block, "Y before X zero replacement");
    RgbaImage image = {};
    assert(decodeImageYuv(block, &image));
    assert(image.pixels[2 * 6 + 2] == 0xff5d6c11u);
    assert(image.pixels[3 * 6 + 2] == 0xff5d49c6u);
    for (u16 height = 1; height <= 6; ++height) {
        for (u16 width = 1; width <= 6; ++width) {
            checkBlock(true, imageBlock(width, height, payload), "zero chroma edges and odd dimensions");
        }
    }
}

std::vector<u8> literalYuvBlock(u16 width, u16 height, const std::vector<u8>& quads) {
    assert(!quads.empty() && quads.size() % 6 == 0 && quads.size() / 6 < 0x8000);
    std::vector<u8> payload;
    append16(&payload, (u16)(0x8000 | (quads.size() / 6)));
    payload.insert(payload.end(), quads.begin(), quads.end());
    return imageBlock(width, height, payload);
}

void checkKnownYuv(const std::vector<u8>& block, const std::vector<u32>& expected,
                   const char* label) {
    // Hard-coded RGB expectations below independently check the new color
    // contract; agreement with the opt-in reference is only an extra check.
    checkBlock(true, block, label);
    RgbaImage image = {};
    ImageDrawInfo info;
    require(decodeImageYuv(block.data(), block.size(), &image, &info), label, "known decode status");
    require(image.pixels == expected, label, "known RGB/alpha values");
    require(sameInfo(info, imageDrawInfo(image)), label, "known image metadata");
}

void checkNeutralMissingChroma() {
    const auto shadow = literalYuvBlock(2, 2, {0, 16, 16, 0, 0, 0});
    checkKnownYuv(shadow, {0, 0xff000000u, 0xff000000u, 0}, "zero-chroma black/transparent checker shadow");
    // Preserve the legacy oracle's default for comparisons with old releases.
    RgbaImage old = {}, corrected = {};
    assert(n32_image_reference::decodeImageYuv(shadow, &old));
    assert(old.pixels == std::vector<u32>({0, 0xff009a00u, 0xff009a00u, 0}));
    assert(n32_image_reference::decodeImageYuv(shadow, &corrected, true));
    assert(corrected.pixels == std::vector<u32>({0, 0xff000000u, 0xff000000u, 0}));

    checkKnownYuv(literalYuvBlock(2, 2, {128, 128, 128, 128, 0, 0}),
                  std::vector<u32>(4, 0xff828282u), "zero U/V becomes neutral gray");
    checkKnownYuv(literalYuvBlock(2, 2, {128, 128, 128, 128, 0, 200}),
                  std::vector<u32>(4, 0xfff54882u), "missing U does not replace nonzero V");
    checkKnownYuv(literalYuvBlock(2, 2, {128, 128, 128, 128, 200, 0}),
                  std::vector<u32>(4, 0xff8266ffu), "missing V does not replace nonzero U");
    checkKnownYuv(literalYuvBlock(2, 2, {145, 145, 145, 145, 53, 34}),
                  std::vector<u32>(4, 0xff00ff00u), "real saturated green remains opaque green");
    checkKnownYuv(literalYuvBlock(2, 2, {0, 0, 0, 0, 200, 200}),
                  std::vector<u32>(4, 0), "Y zero stays transparent with nonzero chroma");
    // An independently decoded color ramp with no missing chroma must remain
    // byte-for-byte equal to the unchanged default oracle, including alpha.
    std::vector<u8> ramp;
    const u8 chroma[] = {1, 16, 54, 128, 160, 200, 240, 255};
    for (size_t i = 0; i < 16; ++i) {
        const u8 quad[] = {0, 16, 128, 235, chroma[i % 8], chroma[(i * 3 + 1) % 8]};
        ramp.insert(ramp.end(), quad, quad + sizeof(quad));
    }
    const auto rampBlock = literalYuvBlock(7, 7, ramp);
    assert(n32_image_reference::decodeImageYuv(rampBlock, &old));
    assert(decodeImageYuv(rampBlock, &corrected) && sameImage(old, corrected));
    checkBlock(true, rampBlock, "nonzero chroma keeps legacy colors and alpha");

    // A zero source component must first try a valid neighbor. Filling 128 in
    // the original planes or before both interpolation passes would fail these.
    checkKnownYuv(literalYuvBlock(4, 2, {128, 128, 128, 128, 0, 128,
                                         128, 128, 128, 128, 200, 128}),
                  {0xff828282u, 0xff8266ffu, 0xff8266ffu, 0xff8266ffu,
                   0xff828282u, 0xff8266ffu, 0xff8266ffu, 0xff8266ffu},
                  "right neighbor precedes neutral fallback at left edge");
    checkKnownYuv(literalYuvBlock(4, 2, {128, 128, 128, 128, 200, 128,
                                         128, 128, 128, 128, 0, 128}),
                  {0xff8266ffu, 0xff8266ffu, 0xff8266ffu, 0xff828282u,
                   0xff8266ffu, 0xff8266ffu, 0xff8266ffu, 0xff828282u},
                  "left neighbor precedes neutral fallback at right edge");
    checkKnownYuv(literalYuvBlock(2, 4, {128, 128, 128, 128, 128, 0,
                                         128, 128, 128, 128, 128, 200}),
                  {0xff828282u, 0xff828282u, 0xfff54882u, 0xfff54882u,
                   0xfff54882u, 0xfff54882u, 0xfff54882u, 0xfff54882u},
                  "lower neighbor precedes neutral fallback at top edge");
    checkKnownYuv(literalYuvBlock(2, 4, {128, 128, 128, 128, 128, 200,
                                         128, 128, 128, 128, 128, 0}),
                  {0xfff54882u, 0xfff54882u, 0xfff54882u, 0xfff54882u,
                   0xfff54882u, 0xfff54882u, 0xff828282u, 0xff828282u},
                  "upper neighbor precedes neutral fallback at bottom edge");

    // Odd dimensions clip the unused quad pixels, including at the final
    // corner; bounds depend on Y transparency, not on the corrected RGB color.
    const auto corner = literalYuvBlock(3, 3, {0, 0, 0, 0, 0, 0,
                                                0, 0, 0, 0, 0, 0,
                                                0, 0, 0, 0, 0, 0,
                                                16, 0, 0, 0, 0, 0});
    std::vector<u32> cornerPixels(9, 0);
    cornerPixels[8] = 0xff000000u;
    checkKnownYuv(corner, cornerPixels, "odd image corner remains opaque black");
    RgbaImage image = {};
    ImageDrawInfo info;
    assert(decodeImageYuv(corner.data(), corner.size(), &image, &info));
    assert(info.left == 2 && info.top == 2 && info.right == 3 && info.bottom == 3);
    assert(!info.allPixelsVisible);
    const auto single = literalYuvBlock(1, 1, {16, 0, 0, 0, 0, 0});
    checkKnownYuv(single, {0xff000000u}, "single opaque black corner");
    assert(decodeImageYuv(single.data(), single.size(), &image, &info));
    assert(info.left == 0 && info.top == 0 && info.right == 1 && info.bottom == 1);
    assert(info.allPixelsVisible);
}

void checkTransparencyAndRuns() {
    std::vector<u8> yuv;
    append16(&yuv, 0x8001);
    const u8 chunk[] = { 0, 16, 235, 0, 128, 128 };
    yuv.insert(yuv.end(), chunk, chunk + sizeof(chunk));
    const std::vector<u8> block = imageBlock(2, 2, yuv);
    checkBlock(true, block, "Y zero transparency and luma ordering");
    RgbaImage image = {};
    assert(decodeImageYuv(block, &image));
    assert(image.pixels[0] == 0 && image.pixels[1] == 0xffffffffu);
    assert(image.pixels[2] == 0xff000000u && image.pixels[3] == 0);

    std::vector<u8> argb;
    append16(&argb, 0xc003); append16(&argb, 0x7fff); // transparent run
    append16(&argb, 0xc005); append16(&argb, 0xffff); // crosses a row
    append16(&argb, 0);                              // transparent pixel
    append16(&argb, 0xc03f); append16(&argb, 0x8000); // clipped overlong run
    checkBlock(false, imageBlock(4, 3, argb), "ARGB transparency and clipped runs");

    std::vector<u8> opaque;
    append16(&opaque, 0xc03f); append16(&opaque, 0xffff);
    checkBlock(false, imageBlock(3, 5, opaque), "fully opaque metadata");
    opaque[2] = 0xff; opaque[3] = 0x7f;
    checkBlock(false, imageBlock(3, 5, opaque), "fully transparent metadata");

    std::vector<u8> yuvRun;
    append16(&yuvRun, 0x7fff);
    yuvRun.insert(yuvRun.end(), chunk, chunk + sizeof(chunk));
    checkBlock(true, imageBlock(5, 7, yuvRun), "YUV clipped run and odd dimensions");
    std::fill(yuvRun.begin() + 2, yuvRun.end(), 0);
    checkBlock(true, imageBlock(5, 7, yuvRun), "fully transparent YUV metadata");
}

void checkTruncation(bool yuv) {
    const std::vector<u8> full = yuv ? generatedYuv(11, 9) : generatedArgb(11, 9);
    for (size_t size = 0; size <= full.size(); ++size) {
        std::vector<u8> prefix(full.begin(), full.begin() + size);
        checkBlock(yuv, prefix, "span shorter than declared payload");
        if (size >= 8) {
            store32(&prefix, 4, (u32)size - 8);
            checkBlock(yuv, prefix, "declared partial stream");
        }
    }
    std::vector<u8> suffix = full;
    suffix.insert(suffix.end(), 16, 0);
    checkBlock(yuv, suffix, "bytes outside declared payload ignored");
}

void checkInvalidAndEmpty() {
    const std::vector<u8> empty;
    for (int format = 0; format != 2; ++format) {
        const bool yuv = format != 0;
        RgbaImage image = {};
        ImageDrawInfo info;
        assert(!decodeSpan(yuv, 0, 0, &image, &info));
        assert(!decodeSpan(yuv, 0, 8, &image, &info));
        checkBlock(yuv, imageBlock(0, 3, empty), "zero width");
        checkBlock(yuv, imageBlock(3, 0, empty), "zero height");
        checkBlock(yuv, imageBlock(4097, 1, empty), "dimension limit");
        checkBlock(yuv, imageBlock(1025, 1024, empty), "pixel count limit");
        checkBlock(yuv, imageBlock(65535, 65535, empty), "overflow-sized raster");
        checkBlock(yuv, imageBlock(1, 1, empty), "empty valid payload");
        std::vector<u8> enormous = imageBlock(3, 3, empty);
        store32(&enormous, 4, 0xffffffffu);
        checkBlock(yuv, enormous, "overflow-sized payload");
    }
    std::vector<u8> zeroOp(2, 0);
    checkBlock(true, imageBlock(2, 2, zeroOp), "YUV invalid zero opcode");
    std::vector<u8> invalid;
    append16(&invalid, 0x8001);
    checkBlock(false, imageBlock(2, 2, invalid), "ARGB invalid literal opcode");
    invalid.clear(); append16(&invalid, 1);
    checkBlock(false, imageBlock(2, 2, invalid), "ARGB invalid low opcode");

    std::vector<u8> noProgress;
    append16(&noProgress, 0x8000);
    append16(&noProgress, 1);
    noProgress.insert(noProgress.end(), 6, 128);
    checkBlock(true, imageBlock(2, 2, noProgress), "YUV empty literal then run");
    noProgress.clear();
    append16(&noProgress, 0xc000); append16(&noProgress, 0xffff);
    append16(&noProgress, 0xc001); append16(&noProgress, 0x8000);
    checkBlock(false, imageBlock(1, 1, noProgress), "ARGB empty run then pixel");
}

void checkReader(bool yuv) {
    const std::vector<u8> block = yuv ? generatedYuv(15, 17) : generatedArgb(15, 17);
    // Odd resource offset also checks the reader's direct, unaligned span.
    std::vector<u8> file(17, 0);
    store32(&file, 4, 17);
    file.insert(file.end(), block.begin(), block.end());
    Native32Reader reader(file);
    reader.imageIdx = 4;
    reader.colorspace = yuv ? ColorspaceYuv : ColorspaceArgb;
    const ImageDrawInfo* info = 0;
    bool allVisible = true;
    const RgbaImage* image = reader.getImageRef(1, &allVisible, &info);
    RgbaImage reference = {};
    assert(referenceDecode(yuv, block, &reference));
    assert(image && info && sameImage(*image, reference));
    assert(sameInfo(*info, imageDrawInfo(reference)));
    assert(allVisible == info->allPixelsVisible);
    assert(reader.imageCacheCount() == 1);
    assert(reader.imageCacheBytes() == reference.pixels.size() * sizeof(u32));
    const ImageDrawInfo* cachedInfo = 0;
    assert(reader.getImageRef(1, 0, &cachedInfo) == image && cachedInfo == info);
}

} // namespace

int main() {
    checkInterpolationOrder();
    checkNeutralMissingChroma();
    checkTransparencyAndRuns();
    checkInvalidAndEmpty();
    for (size_t i = 0; i < 200; ++i) {
        const u16 width = (u16)(1 + randomValue() % 35);
        const u16 height = (u16)(1 + randomValue() % 33);
        checkBlock(true, generatedYuv(width, height), "generated YUV run/literal");
        checkBlock(false, generatedArgb(width, height), "generated ARGB runs");
    }
    checkBlock(true, generatedYuv(320, 240), "full canvas YUV");
    checkBlock(false, generatedArgb(320, 240), "full canvas ARGB");
    checkTruncation(true);
    checkTruncation(false);
    checkReader(true);
    checkReader(false);
    std::printf("image decode: %u differential cases and reader metadata passed\n", checks);
    return 0;
}
