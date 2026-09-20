#include "core/mpeg/buffer.h"
#include "core/mpeg/video.cpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
using namespace n32;
using namespace n32::mpeg;

// Freeze the original coefficient semantics with a separate bit-by-bit tree
// walk. No production prefix table or coefficient helper is used here.
static bool reference(Buffer& buffer, bool haveCoefficient, int* run, int* level) {
    int index = 0, coeff;
    for (;;) {
        const VlcUintEntry& entry = DCT_COEFF[index + buffer.read(1)];
        if (entry.next <= 0) { coeff = entry.value & 0xffff; break; }
        index = entry.next;
    }
    if (coeff == 1 && haveCoefficient && buffer.read(1) == 0) return false;
    if (coeff == 0xffff) {
        *run = (int)buffer.read(6);
        *level = (int)buffer.read(8);
        if (*level == 0) *level = (int)buffer.read(8);
        else if (*level == 128) *level = (int)buffer.read(8) - 256;
        else if (*level > 128) *level -= 256;
    } else {
        *run = coeff >> 8;
        *level = coeff & 255;
        if (buffer.read(1)) *level = -*level;
    }
    return true;
}

static size_t cases;
static void compare(Buffer& scalar, Buffer& lookup, bool haveCoefficient) {
    int scalarRun = -99, scalarLevel = -999;
    int lookupRun = scalarRun, lookupLevel = scalarLevel;
    const bool a = reference(scalar, haveCoefficient, &scalarRun, &scalarLevel);
    const bool b = readDctCoefficient(lookup, haveCoefficient, &lookupRun, &lookupLevel);
    assert(a == b && scalarRun == lookupRun && scalarLevel == lookupLevel);
    assert(scalar.bitIndex == lookup.bitIndex);
    assert(scalar.sourceEnded == lookup.sourceEnded);
    ++cases;
}

static void testPatterns() {
    Buffer scalar(std::vector<u8>(6)), lookup(std::vector<u8>(6));
    // All 17-bit patterns include all 16-bit VLC prefixes and both signs for
    // the longest code. Repeat remaining bits to exercise escape fallbacks.
    for (unsigned state = 0; state < 2; ++state) {
        for (unsigned offset = 0; offset < 8; ++offset) {
            for (unsigned pattern = 0; pattern < 131072; ++pattern) {
                const unsigned long long bits =
                    (((unsigned long long)pattern << 31) |
                     ((unsigned long long)pattern << 14) | (pattern & 0x3fff)) >> offset;
                for (unsigned i = 0; i < 6; ++i)
                    scalar.data[i] = lookup.data[i] = (u8)(bits >> (40 - i * 8));
                scalar.bitIndex = lookup.bitIndex = offset;
                compare(scalar, lookup, state != 0);
            }
        }
    }
    // Every short-tail byte pattern, including cursors beyond physical EOF.
    for (size_t length = 0; length <= 3; ++length) {
        scalar.data.resize(length); lookup.data.resize(length);
        for (unsigned pattern = 0; pattern < 65536; ++pattern) {
            for (size_t i = 0; i < length; ++i)
                scalar.data[i] = lookup.data[i] = (u8)(pattern >> ((i & 1) * 8));
            for (size_t position = 0; position <= length * 8 + 8; ++position) {
                for (unsigned state = 0; state < 2; ++state) {
                    scalar.bitIndex = lookup.bitIndex = position;
                    compare(scalar, lookup, state != 0);
                }
            }
        }
    }
}

static Buffer bitBuffer(const std::string& bits) {
    std::vector<u8> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1') bytes[i / 8] |= (u8)(1 << (7 - i % 8));
    return Buffer(bytes);
}

static std::string field(unsigned value, unsigned bits) {
    std::string text(bits, '0');
    for (unsigned i = 0; i < bits; ++i)
        if ((value >> i) & 1) text[bits - 1 - i] = '1';
    return text;
}

static void testEscapes() {
    // MPEG escape is 000001. Include all first and extension level bytes,
    // negative extended levels and runs at both ends of a block.
    for (unsigned run : {0u, 31u, 63u}) {
        for (unsigned level = 0; level < 256; ++level) {
            for (unsigned extra = 0; extra < 256; ++extra) {
                if (level != 0 && level != 128 && extra != 0) continue;
                const std::string code = "000001" + field(run, 6) +
                    field(level, 8) + field(extra, 8);
                for (unsigned offset = 0; offset < 8; ++offset) {
                    for (unsigned state = 0; state < 2; ++state) {
                        Buffer scalar = bitBuffer(std::string(offset, '0') + code);
                        Buffer lookup = scalar;
                        scalar.bitIndex = lookup.bitIndex = offset;
                        compare(scalar, lookup, state != 0);
                    }
                }
            }
        }
    }
}

class ChunkSource : public ByteSource {
public:
    ChunkSource(const std::vector<u8>& input, size_t chunkSize)
        : bytes(input), chunk(chunkSize), position(0), calls(0) {}
    size_t read(u8* destination, size_t capacity) {
        ++calls;
        const size_t count = std::min(std::min(capacity, chunk), bytes.size() - position);
        if (count) std::memcpy(destination, bytes.data() + position, count);
        position += count;
        return count;
    }
    const std::vector<u8>& bytes;
    size_t chunk, position, calls;
};

static void testStreaming() {
    std::vector<u8> bytes(Buffer::streamLimit + 17);
    u32 seed = 0x434f4546;
    for (u8& byte : bytes) {
        seed = seed * 1664525u + 1013904223u;
        byte = (u8)(seed >> 24);
    }
    for (bool compact : {false, true}) {
        for (size_t chunk : {size_t(1), size_t(2), size_t(7), size_t(16384)}) {
            auto a = std::make_shared<ChunkSource>(bytes, chunk);
            auto b = std::make_shared<ChunkSource>(bytes, chunk);
            Buffer scalar(a), lookup(b);
            unsigned state = 0;
            while (!scalar.hasEnded()) {
                compare(scalar, lookup, (++state & 1) != 0);
                assert(a->position == b->position && a->calls == b->calls);
                assert(scalar.data.size() == lookup.data.size());
                if (compact) { scalar.discardReadBytes(); lookup.discardReadBytes(); }
                assert(scalar.bitIndex == lookup.bitIndex);
            }
            assert(lookup.hasEnded());
            assert(a->position == b->position && a->calls == b->calls);
            for (unsigned i = 0; i < 8; ++i) compare(scalar, lookup, (i & 1) != 0);
        }
    }
}

int main() {
    testPatterns();
    testEscapes();
    testStreaming();
    std::printf("PASS: %zu DCT coefficient cases; all 17-bit patterns/alignment/states, "
                "short tails, escape levels, streaming refills/compaction/cap\n", cases);
}
