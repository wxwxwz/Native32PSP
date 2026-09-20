#include "core/mpeg/buffer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace n32;

// Independent bytewise reference: keep the historical five-byte lookahead,
// including its cursor position at truncated prefixes and EOF.
static s32 referenceNext(mpeg::Buffer& buffer) {
    buffer.align();
    while (buffer.has(40)) {
        size_t at = buffer.bitIndex >> 3;
        if (buffer.data[at] == 0 && buffer.data[at + 1] == 0 && buffer.data[at + 2] == 1) {
            buffer.bitIndex += 32;
            return buffer.data[at + 3];
        }
        buffer.bitIndex += 8;
    }
    return -1;
}

class ChunkedSource : public mpeg::ByteSource {
public:
    ChunkedSource(const std::vector<u8>& input, size_t chunkSize)
        : bytes(input), chunk(chunkSize), position(0), calls(0) {}
    size_t read(u8* out, size_t capacity) {
        ++calls;
        size_t count = std::min(std::min(capacity, chunk), bytes.size() - position);
        if (count) std::memcpy(out, bytes.data() + position, count);
        position += count;
        return count;
    }
    const std::vector<u8>& bytes;
    size_t chunk, position, calls;
};

static void compare(const std::vector<u8>& bytes, size_t start, size_t chunk) {
    auto actualSource = std::make_shared<ChunkedSource>(bytes, chunk);
    auto expectedSource = std::make_shared<ChunkedSource>(bytes, chunk);
    mpeg::Buffer actual = chunk ? mpeg::Buffer(actualSource) : mpeg::Buffer(bytes);
    mpeg::Buffer expected = chunk ? mpeg::Buffer(expectedSource) : mpeg::Buffer(bytes);
    actual.bitIndex = expected.bitIndex = start;
    for (size_t count = 0;; ++count) {
        assert(count <= bytes.size() + 1);
        s32 a = actual.nextStartCode(), b = referenceNext(expected);
        assert(a == b && actual.bitIndex == expected.bitIndex);
        assert(actual.data == expected.data && actual.sourceEnded == expected.sourceEnded);
        assert(actualSource->position == expectedSource->position);
        assert(actualSource->calls == expectedSource->calls);
        if (a == -1) break;
    }
    size_t calls = actualSource->calls, at = actual.bitIndex;
    assert(actual.nextStartCode() == -1 && actual.bitIndex == at);
    assert(actualSource->calls == calls);
}

int main() {
    for (size_t length = 0; length <= 72; ++length) {
        for (size_t prefix = 0; prefix <= length; ++prefix) {
            std::vector<u8> bytes(length, 0xff);
            const u8 marker[] = {0, 0, 1, 0xb3, 0x55};
            for (size_t j = 0; j < sizeof(marker) && prefix + j < length; ++j)
                bytes[prefix + j] = marker[j];
            for (size_t alignment = 0; alignment < 8; ++alignment)
                compare(bytes, alignment, 0);
            for (size_t chunk : {1u, 2u, 3u, 4u, 5u, 7u, 31u, 16384u})
                compare(bytes, 0, chunk);
        }
    }
    for (u8 fill : {0, 255}) {
        std::vector<u8> bytes(66001, fill);
        for (size_t chunk : {0u, 1u, 3u, 7u, 16384u}) compare(bytes, 3, chunk);
        compare(bytes, bytes.size() * 8 + 17, 0);
        compare(bytes, bytes.size() * 8 + 17, 16384);
    }
    std::vector<u8> bytes(mpeg::Buffer::streamLimit + 37);
    u32 state = 0x149832efu;
    for (auto& byte : bytes) { state = state * 1664525u + 1013904223u; byte = state >> 24; }
    for (size_t at = 16380; at + 5 < bytes.size(); at += 16383) {
        bytes[at] = bytes[at + 1] = 0; bytes[at + 2] = 1; bytes[at + 3] = 0xb8;
    }
    for (size_t chunk : {0u, 5u, 31u, 16384u}) compare(bytes, 0, chunk);

    auto source = std::make_shared<ChunkedSource>(bytes, 7);
    mpeg::Buffer lookahead(source);
    lookahead.bitIndex = 3;
    assert(lookahead.hasStartCode(0xb8) == 0xb8 && lookahead.bitIndex == 3);
    size_t calls = source->calls;
    assert(lookahead.hasStartCode(0xb8) == 0xb8 && lookahead.bitIndex == 3);
    assert(source->calls == calls);
    assert(lookahead.nextStartCode() == 0xb8);
    size_t at = lookahead.bitIndex;
    assert(lookahead.hasStartCode(0x100) == -1 && lookahead.bitIndex == at);
    assert(lookahead.data.size() <= mpeg::Buffer::streamLimit);
    assert(lookahead.data.capacity() <= mpeg::Buffer::streamLimit);
    puts("PASS: start-code span scan matches bytewise codes, cursor, refills, lookahead, EOF and stream cap");
}
