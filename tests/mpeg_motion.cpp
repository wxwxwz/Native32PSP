#include "core/mpeg/buffer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// Exercise the file-local reconstruction kernels without exporting test APIs.
#define private public
#include "core/mpeg/video.h"
#undef private
#include "core/mpeg/video.cpp"

using namespace n32;
using namespace n32::mpeg;

static int clampByte(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static int floorHalf(int value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

static void referenceMotion(const std::vector<u8>& source, std::vector<u8>& dest,
                            int row, int col, int width, int height,
                            int mh, int mv, int size, bool interpolate) {
    const int stride = width * size;
    const int sx = col * size + floorHalf(mh);
    const int sy = row * size + floorHalf(mv);
    const long long start = (long long)sy * stride + sx;
    const long long target = ((long long)row * stride + col) * size;
    const long long last = (long long)stride * (height * size - size + 1) - size;
    if (start < 0 || start > last || target < 0 || target > last) return;

    const int sampleWidth = mh % 2 != 0 ? 2 : 1;
    const int sampleHeight = mv % 2 != 0 ? 2 : 1;
    const int samples = sampleWidth * sampleHeight;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int sum = 0;
            for (int dy = 0; dy < sampleHeight; ++dy)
                for (int dx = 0; dx < sampleWidth; ++dx)
                    sum += source[(size_t)(start + (y + dy) * stride + x + dx)];
            int value = (sum + samples / 2) / samples;
            const size_t at = (size_t)(target + y * stride + x);
            if (interpolate) value = (dest[at] + value + 1) / 2;
            dest[at] = (u8)value;
        }
    }
}

static size_t testMotion() {
    size_t cases = 0;
    unsigned int random = 1234567;
    for (int size : {8, 16}) {
        const int width = 4, height = 3;
        const size_t nominal = width * size * height * size;
        // The production address rejection predates this optimization and does
        // not exclude every half-pixel edge read. Guard storage lets us compare
        // its exact existing acceptance semantics, including those edges.
        std::vector<u8> source(nominal + width * size + 1);
        std::vector<u8> initial(source.size());
        for (size_t i = 0; i < source.size(); ++i) {
            random = random * 1664525u + 1013904223u;
            source[i] = (u8)(random >> 24);
            random = random * 1664525u + 1013904223u;
            initial[i] = (u8)(random >> 24);
        }
        for (int row = -1; row <= height; ++row)
            for (int col = -1; col <= width; ++col)
                for (int mv = -2 * size - 3; mv <= 2 * size + 3; ++mv)
                    for (int mh = -2 * size - 3; mh <= 2 * size + 3; ++mh)
                        for (int interpolate = 0; interpolate != 2; ++interpolate) {
                            std::vector<u8> expected(initial), actual(initial);
                            referenceMotion(source, expected, row, col, width, height,
                                            mh, mv, size, interpolate != 0);
                            processMacroblock(source, actual, row, col, width, height,
                                              mh, mv, size, interpolate != 0);
                            assert(actual == expected);
                            ++cases;
                        }
    }
    return cases;
}

static void testChromaMotion() {
    Video video{std::vector<u8>()};
    video.mWidth = video.lumaWidth = 64;
    video.mHeight = video.lumaHeight = 48;
    video.chromaWidth = 32;
    video.chromaHeight = 24;
    video.mbWidth = 4;
    video.mbHeight = 3;
    video.mbSize = 12;
    video.frames.push_back(video.makeFrame());
    video.frames.push_back(video.makeFrame());
    unsigned int random = 98987;
    for (Frame& frame : video.frames)
        for (Plane* plane : {&frame.y, &frame.cb, &frame.cr}) {
            plane->data.resize(plane->data.size() + plane->width + 1);
            for (u8& value : plane->data) {
                random = random * 1664525u + 1013904223u;
                value = (u8)(random >> 24);
            }
        }
    const Frame initial(video.frames[0]);
    const Frame& source = video.frames[1];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            for (int mv = -11; mv <= 11; ++mv)
                for (int mh = -11; mh <= 11; ++mh)
                    for (int interpolate = 0; interpolate < 2; ++interpolate) {
                        video.frames[0] = initial;
                        Frame expected(initial);
                        referenceMotion(source.y.data, expected.y.data, row, col, 4, 3,
                                        mh, mv, 16, interpolate != 0);
                        // Chroma motion truncates toward zero before its own
                        // half-pixel decomposition, even for negative odd values.
                        referenceMotion(source.cb.data, expected.cb.data, row, col, 4, 3,
                                        mh / 2, mv / 2, 8, interpolate != 0);
                        referenceMotion(source.cr.data, expected.cr.data, row, col, 4, 3,
                                        mh / 2, mv / 2, 8, interpolate != 0);
                        video.mbRow = row;
                        video.mbCol = col;
                        video.copyOrInterpolateMacroblock(1, mh, mv, interpolate != 0);
                        assert(video.frames[0].y.data == expected.y.data);
                        assert(video.frames[0].cb.data == expected.cb.data);
                        assert(video.frames[0].cr.data == expected.cr.data);
                    }
}

static void testBlockWrites() {
    unsigned int random = 555;
    for (int stride : {8, 13, 32}) {
        for (int offset : {0, 1, 17}) {
            std::vector<u8> initial((size_t)offset + stride * 8 + 19);
            int samples[64];
            for (size_t i = 0; i < initial.size(); ++i) {
                random = random * 1664525u + 1013904223u;
                initial[i] = (u8)(random >> 24);
            }
            for (int i = 0; i < 64; ++i) {
                random = random * 1664525u + 1013904223u;
                samples[i] = (int)(random % 4096) - 2048;
            }
            for (int mode = 0; mode < 4; ++mode)
                for (int value : {-2048, -256, -1, 0, 1, 127, 255, 256, 2047}) {
                    std::vector<u8> expected(initial), actual(initial);
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x) {
                            size_t at = (size_t)offset + y * stride + x;
                            int v = mode == 0 || mode == 2 ? value : samples[y * 8 + x];
                            if (mode >= 2) v += expected[at];
                            expected[at] = (u8)(mode == 0 ? v : clampByte(v));
                        }
                    if (mode == 0) blockSetConst(actual, offset, stride, value);
                    if (mode == 1) blockSetOverwrite(actual, offset, stride, samples);
                    if (mode == 2) blockSetAddConst(actual, offset, stride, value);
                    if (mode == 3) blockSetAdd(actual, offset, stride, samples);
                    assert(actual == expected);
                }
        }
    }
}

template<class Entry>
static bool findCode(const Entry* tree, int index, int value, std::string* code) {
    for (int bit = 0; bit < 2; ++bit) {
        const Entry& entry = tree[index + bit];
        code->push_back((char)('0' + bit));
        if ((entry.next == 0 && entry.value == value) ||
            (entry.next > 0 && findCode(tree, entry.next, value, code))) return true;
        code->pop_back();
    }
    return false;
}

static Buffer bitBuffer(const std::string& bits) {
    std::vector<u8> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1') bytes[i / 8] |= (u8)(1 << (7 - i % 8));
    return Buffer(bytes);
}

static void assertClear(const Video& video) {
    for (int i = 0; i < 64; ++i) assert(video.blockData[i] == 0);
}

static void testCoefficientCleanup() {
    Video video{std::vector<u8>()};
    video.mWidth = video.mHeight = video.lumaWidth = video.lumaHeight = 16;
    video.chromaWidth = video.chromaHeight = 8;
    video.mbWidth = video.mbHeight = video.mbSize = 1;
    video.frames.push_back(video.makeFrame());
    video.macroblockIntra = true;
    video.quantizerScale = 8;
    std::fill(video.intraQuantMatrix, video.intraQuantMatrix + 64, 16);
    std::fill(video.nonIntraQuantMatrix, video.nonIntraQuantMatrix + 64, 16);

    std::string dc, escape, eob;
    assert(findCode(DCT_SIZE_LUMINANCE, 0, 0, &dc));
    assert(findCode(DCT_COEFF, 0, 0xffff, &escape));
    assert(findCode(DCT_COEFF, 0, 1, &eob));
    eob += '0';
    const std::string ac = escape + "000000" + "00000001";
    const std::string invalid = dc + ac + escape + "111111" + "00000001";

    // A run past coefficient 63 intentionally retains the old partial block.
    video.buffer = bitBuffer(invalid);
    video.decodeBlock(0);
    assert(video.blockData[0] == 128 * 256);
    assert(video.blockData[1] != 0);
    assert(video.frames[0].y.data == std::vector<u8>(256, 0));

    // The following DC-only block must ignore and then clear all stale AC.
    video.buffer = bitBuffer(dc + eob);
    video.decodeBlock(0);
    assertClear(video);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            assert(video.frames[0].y.data[y * 16 + x] == (x < 8 && y < 8 ? 128 : 0));

    // Both the IDCT and DC-only paths clear coefficients when no frame exists.
    video.cur = -1;
    for (const std::string& bits : {dc + eob, dc + ac + eob}) {
        std::fill(video.blockData, video.blockData + 64, 7);
        video.buffer = bitBuffer(bits);
        video.decodeBlock(0);
        assertClear(video);
    }

    // Non-intra DC-only addition also ignores stale AC and clears every entry.
    video.cur = 0;
    video.macroblockIntra = false;
    std::fill(video.blockData, video.blockData + 64, 7);
    video.buffer = bitBuffer(ac + eob);
    video.decodeBlock(0);
    assertClear(video);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) assert(video.frames[0].y.data[y * 16 + x] == 131);
}

int main() {
    const size_t cases = testMotion();
    testChromaMotion();
    testBlockWrites();
    testCoefficientCleanup();
    std::printf("PASS: %zu motion cases, 8/16 blocks, all 8 modes, negative motion, "
                "address rejection, block saturation and malformed-block cleanup\n", cases);
}
