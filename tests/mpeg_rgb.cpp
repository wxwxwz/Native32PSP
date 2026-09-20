#include "core/mpeg/video.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdint.h>

#ifndef NATIVE32_MPEG_RGB_SANITIZE
static size_t allocationCount;
void* operator new(size_t size) {
    ++allocationCount;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }
#endif

// Frozen pre-optimization production converter from the 0125 source snapshot.
// Benchmark this table-based converter, not the slower direct-math oracle below.
// The inherited planes are initialized before timing; no frame copies are timed.
namespace frozen {
struct Frame : n32::mpeg::Frame {
    void writeRgbScaled(std::vector<u32>* dst, size_t dstW, size_t dstH) const;
};
static int clampU8(int value) { return std::max(0, std::min(255, value)); }
// Preserve the exact integer conversion, including green's single rounding
// after adding both chroma products. About 5 KiB replaces repeated multiplies.
struct YuvTables {
    int y[256], r[256], b[256], gc[256], gr[256];
    u8 clamp[1024];
    YuvTables() {
        for (int i = 0; i < 256; ++i) {
            y[i] = ((i - 16) * 76309) >> 16;
            r[i] = ((i - 128) * 104597) >> 16;
            b[i] = ((i - 128) * 132201) >> 16;
            gc[i] = (i - 128) * 25674;
            gr[i] = (i - 128) * 53278;
        }
        // All converted components lie in [-278, 534]. Bias by 320 so every
        // lookup stays inside this small table, including extreme chroma.
        for (int i = 0; i < 1024; ++i) clamp[i] = (u8)clampU8(i - 320);
    }
};
static const YuvTables yuv;

static inline u32 rgbFromLuma(u8 luma, int r, int g, int b) {
    const int yy = yuv.y[luma];
    return 0xff000000u | ((u32)yuv.clamp[yy + r + 320] << 16) |
        ((u32)yuv.clamp[yy - g + 320] << 8) | (u32)yuv.clamp[yy + b + 320];
}

void Frame::writeRgbScaled(std::vector<u32>* dst, size_t dstW, size_t dstH) const {
    if (!dst || width == 0 || height == 0 || dstW == 0 || dstH == 0) {
        return;
    }
    size_t sourceWidth = width > 1 ? width : 1;
    size_t sourceHeight = height > 1 ? height : 1;
    const size_t total = dstW * dstH;
    if (dst->size() != total) {
        dst->resize(total);
    }
    const size_t chromaWidth = sourceWidth / 2 + sourceWidth % 2;
    const size_t chromaHeight = sourceHeight / 2 + sourceHeight % 2;
    if (y.width < sourceWidth || cr.width < chromaWidth || cb.width < chromaWidth ||
        y.data.size() / y.width < sourceHeight ||
        cr.data.size() / cr.width < chromaHeight || cb.data.size() / cb.width < chromaHeight) {
        std::fill(dst->begin(), dst->end(), 0xff000000u);
        return;
    }

    // Native32 cutscenes are normally rendered at their decoded size. Avoid
    // the per-pixel multiply/divide used by the general scaler in that case;
    // this is a sizeable win on the PSP's software MPEG path.
    if (dstW == sourceWidth && dstH == sourceHeight) {
        // A 2x2 luma block shares one chroma sample in MPEG 4:2:0. Compute
        // its colour contribution once, preserving the integer rounding.
        for (size_t sy = 0; sy < sourceHeight; sy += 2) {
            const u8* yRow = &y.data[sy * y.width];
            const u8* crRow = &cr.data[(sy >> 1) * cr.width];
            const u8* cbRow = &cb.data[(sy >> 1) * cb.width];
            u32* out = &(*dst)[sy * dstW];
            const bool secondRow = sy + 1 < sourceHeight;
            for (size_t sx = 0; sx < sourceWidth; sx += 2) {
                int crValue = crRow[sx >> 1];
                int cbValue = cbRow[sx >> 1];

                int r = yuv.r[crValue];
                int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                int b = yuv.b[cbValue];

                out[sx] = rgbFromLuma(yRow[sx], r, g, b);
                if (sx + 1 < sourceWidth) out[sx + 1] = rgbFromLuma(yRow[sx + 1], r, g, b);
                if (secondRow) {
                    out[dstW + sx] = rgbFromLuma(yRow[y.width + sx], r, g, b);
                    if (sx + 1 < sourceWidth)
                        out[dstW + sx + 1] = rgbFromLuma(yRow[y.width + sx + 1], r, g, b);
                }
            }
        }
        return;
    }

    // Exact nearest-neighbour stepping without a division per output pixel.
    const size_t xStep = sourceWidth / dstW;
    const size_t xRemainder = sourceWidth % dstW;
    for (size_t ty = 0; ty < dstH; ++ty) {
        size_t sy = ty * sourceHeight / dstH;
        if (sy >= sourceHeight) {
            sy = sourceHeight - 1;
        }
        size_t yRow = sy * y.width;
        size_t cRow = (sy / 2) * cr.width;
        size_t dstRow = ty * dstW;
        size_t sx = 0, xError = 0;
        for (size_t tx = 0; tx < dstW; ++tx) {
            int yv = (int)y.data[yRow + sx];
            size_t ci = cRow + (sx / 2);
            int crValue = cr.data[ci];
            int cbValue = cb.data[(sy / 2) * cb.width + sx / 2];

            int yy = yuv.y[yv];
            int r = yuv.r[crValue];
            int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
            int b = yuv.b[cbValue];

            int rr = yuv.clamp[yy + r + 320];
            int gg = yuv.clamp[yy - g + 320];
            int bb = yuv.clamp[yy + b + 320];
            (*dst)[dstRow + tx] = 0xff000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
            sx += xStep;
            xError += xRemainder;
            if (xError >= dstW) {
                xError -= dstW;
                ++sx;
            }
        }
    }
}

} // namespace frozen

namespace {

typedef n32::mpeg::Frame Frame;
static size_t checkedCases;
static uint64_t digest = UINT64_C(14695981039346656037);

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static uint64_t hashPixels(const std::vector<u32>& pixels) {
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < pixels.size(); ++i) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value ^= (pixels[i] >> shift) & 255;
            value *= UINT64_C(1099511628211);
        }
    }
    return value;
}

// Independent direct arithmetic, with green rounded only after summing both
// products. MPEG zero chroma is valid; it does not use image-decoder recovery.
static u32 scalarPixel(u8 luma, u8 cr, u8 cb) {
    const int yy = ((static_cast<int>(luma) - 16) * 76309) >> 16;
    const int r = ((static_cast<int>(cr) - 128) * 104597) >> 16;
    const int b = ((static_cast<int>(cb) - 128) * 132201) >> 16;
    const int g = ((static_cast<int>(cb) - 128) * 25674 +
                   (static_cast<int>(cr) - 128) * 53278) >> 16;
    const u32 rr = static_cast<u32>(std::max(0, std::min(255, yy + r)));
    const u32 gg = static_cast<u32>(std::max(0, std::min(255, yy - g)));
    const u32 bb = static_cast<u32>(std::max(0, std::min(255, yy + b)));
    return 0xff000000u | (rr << 16) | (gg << 8) | bb;
}

static std::vector<u32> scalarImage(const Frame& frame, size_t width, size_t height) {
    std::vector<u32> pixels(width * height);
    for (size_t ty = 0; ty < height; ++ty) {
        const size_t sy = ty * frame.height / height;
        for (size_t tx = 0; tx < width; ++tx) {
            const size_t sx = tx * frame.width / width;
            pixels[ty * width + tx] = scalarPixel(
                frame.y.data[sy * frame.y.width + sx],
                frame.cr.data[(sy / 2) * frame.cr.width + sx / 2],
                frame.cb.data[(sy / 2) * frame.cb.width + sx / 2]);
        }
    }
    return pixels;
}

static void fillPlane(n32::mpeg::Plane* plane, size_t width, size_t height,
                      size_t padding, unsigned seed) {
    plane->width = width + padding;
    plane->height = height;
    plane->data.assign(plane->width * height, 0xa5);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            seed = seed * 1664525u + 1013904223u;
            plane->data[y * plane->width + x] = static_cast<u8>(seed >> 24);
        }
    }
}

static frozen::Frame makeFrame(size_t width, size_t height, size_t padding = 0) {
    frozen::Frame frame;
    frame.width = width;
    frame.height = height;
    fillPlane(&frame.y, width, height, padding, 0x19283746u);
    fillPlane(&frame.cr, (width + 1) / 2, (height + 1) / 2,
              padding ? padding + 3 : 0, 0x61728394u);
    fillPlane(&frame.cb, (width + 1) / 2, (height + 1) / 2,
              padding ? padding + 7 : 0, 0xa1b2c3d4u);
    return frame;
}

static __attribute__((noinline)) void convertBefore(const frozen::Frame& frame,
    std::vector<u32>* pixels, size_t width, size_t height) {
    frame.writeRgbScaled(pixels, width, height);
}

static __attribute__((noinline)) void convertAfter(const frozen::Frame& frame,
    std::vector<u32>* pixels, size_t width, size_t height) {
    static_cast<const Frame&>(frame).writeRgbScaled(pixels, width, height);
}

static void comparePixels(const std::vector<u32>& actual,
                          const std::vector<u32>& expected, const char* label) {
    require(actual.size() == expected.size(), "output size mismatch");
    for (size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::fprintf(stderr, "FAIL: %s pixel %zu: got %08x, expected %08x\n",
                         label, i, actual[i], expected[i]);
            std::exit(1);
        }
    }
}

static void checkFrame(const frozen::Frame& frame, size_t width, size_t height,
                       const char* label) {
    const std::vector<u32> expected = scalarImage(frame, width, height);
    std::vector<u32> before(3, 0x12345678u), after(7, 0x87654321u);
    convertBefore(frame, &before, width, height);
    convertAfter(frame, &after, width, height);
    comparePixels(before, expected, "frozen converter versus scalar");
    comparePixels(after, expected, label);
    digest ^= hashPixels(after);
    digest *= UINT64_C(1099511628211);
    ++checkedCases;
}

static void testDimensions() {
    const size_t cases[][4] = {
        {352, 288, 320, 240}, {320, 240, 320, 240},
        {351, 287, 319, 239}, {640, 360, 480, 272},
        {640, 480, 320, 240}, {720, 576, 320, 240},
        {17, 13, 320, 240}, {320, 240, 17, 13},
        {1, 1, 1, 1}, {1, 1, 480, 272}, {1, 37, 480, 272},
        {39, 1, 320, 240}, {319, 239, 1, 37}, {351, 287, 39, 1},
        {511, 3, 512, 7}, {513, 3, 512, 7}, {1024, 3, 512, 7},
        {1025, 3, 512, 7}, {17, 13, 513, 259}, {17, 13, 512, 259},
        {65535, 1, 512, 2}, {65536, 1, 512, 2}, {65536, 1, 511, 2},
        {65537, 1, 512, 2}, {65537, 1, 513, 2},
        {65536, 1, 1, 2}, {65537, 1, 1, 2},
        {1, 65537, 3, 511}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (size_t padding = 0; padding < 2; ++padding) {
            const frozen::Frame frame = makeFrame(cases[i][0], cases[i][1], padding * 5);
            checkFrame(frame, cases[i][2], cases[i][3], "dimension/pitch boundary");
        }
    }
    // Exhaustive small source/destination geometry catches phase drift, odd
    // chroma edges, collapsed axes and repeated source rows/columns.
    for (size_t sourceH = 1; sourceH <= 9; ++sourceH) {
        for (size_t sourceW = 1; sourceW <= 9; ++sourceW) {
            const frozen::Frame frame = makeFrame(sourceW, sourceH, (sourceW + sourceH) % 3);
            for (size_t height = 1; height <= 9; ++height)
                for (size_t width = 1; width <= 9; ++width)
                    checkFrame(frame, width, height, "exhaustive tiny geometry");
        }
    }
}

static void testColorRounding() {
    // Every pair of 8-bit chroma values, on unscaled and mapped paths.
    frozen::Frame frame = makeFrame(512, 512, 3);
    for (size_t cr = 0; cr < 256; ++cr) {
        for (size_t cb = 0; cb < 256; ++cb) {
            frame.cr.data[cr * frame.cr.width + cb] = static_cast<u8>(cr);
            frame.cb.data[cr * frame.cb.width + cb] = static_cast<u8>(cb);
        }
    }
    checkFrame(frame, 512, 512, "all chroma pairs native");
    checkFrame(frame, 512, 513, "all chroma pairs scaled");
    const u8 edge[] = {0, 1, 15, 16, 17, 127, 128, 129, 234, 235, 236, 254, 255};
    frozen::Frame pixel = makeFrame(1, 1);
    for (size_t y = 0; y < sizeof(edge); ++y) {
        for (size_t cr = 0; cr < sizeof(edge); ++cr) {
            for (size_t cb = 0; cb < sizeof(edge); ++cb) {
                pixel.y.data[0] = edge[y];
                pixel.cr.data[0] = edge[cr];
                pixel.cb.data[0] = edge[cb];
                checkFrame(pixel, 3, 5, "extreme luma/chroma");
            }
        }
    }
    pixel.y.data[0] = 16;
    pixel.cr.data[0] = pixel.cb.data[0] = 0;
    std::vector<u32> output;
    convertAfter(pixel, &output, 3, 5);
    require(output[0] == 0xff009b00u, "MPEG zero chroma must retain legitimate green");
    pixel.y.data[0] = 0;
    convertAfter(pixel, &output, 3, 5);
    require((output[0] >> 24) == 255, "MPEG zero luma must remain opaque");
}

static void testInvalidAndReuse() {
    const frozen::Frame original = makeFrame(7, 5, 3);
    const std::vector<u32> sentinel(11, 0x12abcdefu);
    for (unsigned condition = 0; condition < 4; ++condition) {
        frozen::Frame frame = original;
        size_t width = 13, height = 9;
        if (condition == 0) frame.width = 0;
        if (condition == 1) frame.height = 0;
        if (condition == 2) width = 0;
        if (condition == 3) height = 0;
        std::vector<u32> output = sentinel;
        convertAfter(frame, &output, width, height);
        require(output == sentinel, "zero dimensions must leave output untouched");
        convertAfter(frame, 0, width, height);
    }
    convertAfter(original, 0, 13, 9);

    // Validation uses stored row pitches and available bytes, not Plane.height.
    frozen::Frame misleadingHeight = original;
    misleadingHeight.y.height = misleadingHeight.cr.height = misleadingHeight.cb.height = 0;
    checkFrame(misleadingHeight, 13, 9, "independent plane height metadata");
    for (unsigned planeIndex = 0; planeIndex < 3; ++planeIndex) {
        for (unsigned damage = 0; damage < 4; ++damage) {
            frozen::Frame frame = original;
            n32::mpeg::Plane* plane = planeIndex == 0 ? &frame.y :
                (planeIndex == 1 ? &frame.cr : &frame.cb);
            if (damage == 0) plane->width = 0;
            if (damage == 1) plane->width = (planeIndex == 0 ? frame.width : (frame.width + 1) / 2) - 1;
            if (damage == 2) plane->data.pop_back();
            if (damage == 3) plane->data.clear();
            const size_t outputWidths[] = {7, 13, 513};
            for (size_t i = 0; i < 3; ++i) {
                const size_t width = outputWidths[i], height = i == 0 ? 5 : 9;
                std::vector<u32> output = sentinel, before = sentinel;
                convertAfter(frame, &output, width, height);
                convertBefore(frame, &before, width, height);
                require(output == before, "invalid planes must preserve baseline behavior");
                require(output == std::vector<u32>(width * height, 0xff000000u),
                        "invalid plane must resize and fill opaque black");
                ++checkedCases;
            }
        }
    }
    frozen::Frame frame = makeFrame(352, 288, 7);
    std::vector<u32> output(320 * 240, 0);
    convertAfter(frame, &output, 320, 240);
    u32* const storage = output.data();
#ifndef NATIVE32_MPEG_RGB_SANITIZE
    const size_t beforeAllocations = allocationCount;
#endif
    for (unsigned i = 0; i < 16; ++i) {
        frame.y.data[i] = static_cast<u8>(i);
        frame.cr.data[i] = static_cast<u8>(i * 17);
        frame.cb.data[i] = static_cast<u8>(255 - i * 17);
        convertAfter(frame, &output, 320, 240);
    }
#ifndef NATIVE32_MPEG_RGB_SANITIZE
    require(allocationCount == beforeAllocations, "same-size conversions must not allocate heap scratch");
#endif
    require(output.data() == storage, "same-size output must reuse storage");
    comparePixels(output, scalarImage(frame, 320, 240), "frame changes must invalidate chroma reuse");
    convertAfter(frame, &output, 13, 9);
    comparePixels(output, scalarImage(frame, 13, 9), "output shrink");
    convertAfter(frame, &output, 480, 272);
    comparePixels(output, scalarImage(frame, 480, 272), "output growth");
}

typedef void (*Converter)(const frozen::Frame&, std::vector<u32>*, size_t, size_t);

static void benchmark(bool before) {
    struct Case {
        const char* name;
        size_t sourceW, sourceH, targetW, targetH;
        unsigned iterations;
    };
    const Case cases[] = {
        {"logo_352x288_to_320x240", 352, 288, 320, 240, 1024},
        {"native_320x240", 320, 240, 320, 240, 1024},
        {"odd_351x287_to_319x239", 351, 287, 319, 239, 1024},
        {"wide_640x360_to_480x272", 640, 360, 480, 272, 1024},
        {"downscale_640x480_to_320x240", 640, 480, 320, 240, 1024},
        {"downscale_720x576_to_320x240", 720, 576, 320, 240, 1024},
        {"upscale_17x13_to_320x240", 17, 13, 320, 240, 1024},
        {"fallback_17x13_to_513x259", 17, 13, 513, 259, 1024},
        {"fallback_65537x1_to_320x2", 65537, 1, 320, 2, 16384}
    };
    const Converter convert = before ? convertBefore : convertAfter;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        const Case& item = cases[c];
        frozen::Frame frame = makeFrame(item.sourceW, item.sourceH, 5);
        std::vector<u32> output(item.targetW * item.targetH);
        const std::vector<u32> expected = scalarImage(frame, item.targetW, item.targetH);
        for (unsigned i = 0; i < 16; ++i) convert(frame, &output, item.targetW, item.targetH);
        comparePixels(output, expected, "benchmark warmup");
#ifndef NATIVE32_MPEG_RGB_SANITIZE
        const size_t startAllocations = allocationCount;
#endif
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < item.iterations; ++i) {
            frame.y.data[i % frame.y.data.size()] = static_cast<u8>(i * 37);
            convert(frame, &output, item.targetW, item.targetH);
        }
        const auto end = std::chrono::steady_clock::now();
#ifndef NATIVE32_MPEG_RGB_SANITIZE
        const long long allocations = static_cast<long long>(allocationCount - startAllocations);
#else
        const long long allocations = -1;
#endif
        const long long micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        comparePixels(output, scalarImage(frame, item.targetW, item.targetH), "benchmark final pixels");
        std::printf("%s %s iterations=%u us=%lld us_per_frame=%.3f allocations=%lld checksum=%016llx\n",
                    before ? "before" : "after", item.name, item.iterations, micros,
                    static_cast<double>(micros) / item.iterations, allocations,
                    static_cast<unsigned long long>(hashPixels(output)));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--before") || !std::strcmp(argv[1], "--after"))) {
        benchmark(!std::strcmp(argv[1], "--before"));
        return 0;
    }
    if (argc != 1) {
        std::fprintf(stderr, "Usage: %s [--before|--after]\n", argv[0]);
        return 2;
    }
    testDimensions();
    testColorRounding();
    testInvalidAndReuse();
    std::printf("PASS: MPEG RGB %zu pixel-equivalence cases, invalid planes, zero dimensions, output reuse; checksum=%016llx\n",
                checkedCases, static_cast<unsigned long long>(digest));
    return 0;
}
