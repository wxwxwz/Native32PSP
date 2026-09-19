#include "core/mpeg/buffer.h"
#include <cassert>
#include <cstdio>
using namespace n32;
class ZeroStream:public mpeg::ByteSource {
public:
    size_t consumed=0;
    size_t read(u8* destination,size_t size){for(size_t i=0;i<size;++i)destination[i]=0;consumed+=size;return size;}
};
int main() {
    auto source=std::make_shared<ZeroStream>();
    mpeg::Buffer limited(source);
    assert(limited.findStartCode(0xb3)==-1);
    assert(source->consumed<=mpeg::Buffer::streamLimit && limited.data.capacity()<=mpeg::Buffer::streamLimit);

    u32 seed = 1234567;
    for (size_t size = 0; size <= 65; ++size) {
        std::vector<u8> bytes(size);
        for (u8& byte : bytes) {
            seed = seed * 1664525u + 1013904223u;
            byte = (u8)(seed >> 24);
        }
        mpeg::Buffer buffer(bytes);
        for (size_t pos = 0; pos <= size * 8 + 8; ++pos) {
            for (size_t count = 0; count <= 40; ++count) {
                const bool valid = pos <= size * 8 && count <= size * 8 - pos;
                u32 expected = 0;
                if (valid) for (size_t bit = pos; bit < pos + count; ++bit)
                    expected = (expected << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1);
                buffer.bitIndex = pos;
                assert(buffer.read(count) == expected);
                assert(buffer.bitIndex == (valid ? pos + count : pos));
            }
        }
    }
    std::puts("PASS: bit fields 0..40, all alignments, empty/truncated tails and cursor preservation");
}
