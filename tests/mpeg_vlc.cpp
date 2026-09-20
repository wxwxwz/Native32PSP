#include "core/mpeg/buffer.h"
// Keep the production MPEG codebook in this test's translation unit, without
// exposing decoder internals or maintaining a second copy of the VLC tree.
#include "core/mpeg/video.cpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace n32;
using namespace n32::mpeg;

static int reference(Buffer& buffer, const VlcUintEntry* tree) {
    short index = 0;
    for (;;) {
        const VlcUintEntry& entry = tree[index + buffer.read(1)];
        if (entry.next <= 0) return entry.value & 0xffff;
        index = entry.next;
    }
}

static void compare(Buffer& slow, Buffer& fast) {
    assert(reference(slow, DCT_COEFF) == fast.readVlcUint(DCT_COEFF_PREFIX));
    assert(slow.bitIndex == fast.bitIndex);
}

class ChunkSource : public ByteSource {
public:
    ChunkSource(const std::vector<u8>& input, size_t chunkSize)
        : bytes(input), chunk(chunkSize), position(0), calls(0) {}
    size_t read(u8* destination, size_t capacity) {
        ++calls;
        size_t count = std::min(std::min(capacity, chunk), bytes.size() - position);
        if (count) std::memcpy(destination, bytes.data() + position, count);
        position += count;
        return count;
    }
    std::vector<u8> bytes;
    size_t chunk, position, calls;
};

int main() {
    Buffer slow(std::vector<u8>(3)), fast(std::vector<u8>(3));
    // MPEG-1's DCT codes are at most 16 bits. Enumerating every 16-bit
    // pattern exercises every leaf, invalid leaf and long-code continuation.
    for (unsigned offset = 0; offset < 8; ++offset) {
        for (unsigned pattern = 0; pattern < 65536; ++pattern) {
            u32 bits = pattern << (8 - offset);
            for (unsigned i = 0; i < 3; ++i)
                slow.data[i] = fast.data[i] = (u8)(bits >> (16 - i * 8));
            slow.bitIndex = fast.bitIndex = offset;
            compare(slow, fast);
        }
    }

    // The scalar reader supplies zero without advancing at EOF. Cover empty
    // input, short tails and cursors already beyond the physical buffer.
    for (size_t length = 0; length <= 3; ++length) {
        slow.data.resize(length); fast.data.resize(length);
        for (unsigned pattern = 0; pattern < 65536; ++pattern) {
            for (size_t i = 0; i < length; ++i)
                slow.data[i] = fast.data[i] = (u8)(pattern >> ((i & 1) * 8));
            for (size_t position = 0; position <= length * 8 + 8; ++position) {
                slow.bitIndex = fast.bitIndex = position;
                compare(slow, fast);
            }
        }
    }

    std::vector<u8> bytes(70000);
    u32 seed = 0x31564c43;
    for (u8& byte : bytes) {
        seed = seed * 1664525u + 1013904223u;
        byte = (u8)(seed >> 24);
    }
    for (size_t chunk : {size_t(1), size_t(2), size_t(7), size_t(16384)}) {
        auto a = std::make_shared<ChunkSource>(bytes, chunk);
        auto b = std::make_shared<ChunkSource>(bytes, chunk);
        Buffer scalar(a), lookup(b);
        while (!scalar.hasEnded()) {
            compare(scalar, lookup);
            assert(a->position == b->position && a->calls == b->calls);
            scalar.discardReadBytes(); lookup.discardReadBytes();
            assert(scalar.bitIndex == lookup.bitIndex);
        }
        // hasEnded above is itself allowed to pull EOF once on the reference.
        assert(lookup.hasEnded());
        for (int i = 0; i < 8; ++i) compare(scalar, lookup);
    }
    std::puts("PASS: DCT VLC exhaustive 16-bit patterns/all alignments, exact short-tail cursors, streaming refills and compaction");
}
