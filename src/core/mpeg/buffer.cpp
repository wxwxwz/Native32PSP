#include "core/mpeg/buffer.h"
#include <algorithm>
#include <utility>

namespace n32 {
namespace mpeg {

const size_t Buffer::streamLimit;

Buffer::Buffer() : bitIndex(0) {
}

Buffer::Buffer(std::vector<u8> bytes) : data(std::move(bytes)), bitIndex(0) {
}

Buffer::Buffer(std::shared_ptr<ByteSource> input) : source(input),bitIndex(0) {data.reserve(65536);}

size_t Buffer::lenBits() const {
    return data.size() << 3;
}

bool Buffer::has(size_t bits) const {
    if(source && !sourceEnded) {
        if(bits>streamLimit*8 || bitIndex>streamLimit*8-bits){sourceEnded=true;return false;}
        size_t required=(bitIndex+bits+7)/8;
        while(data.size()<required && !sourceEnded) {
            size_t before=data.size(),chunk=std::min((size_t)16384,streamLimit-before);
            if(!chunk){sourceEnded=true;break;}
            // std::vector may otherwise grow geometrically past the hard cap.
            if(before+chunk>data.capacity())
                data.reserve(std::min(streamLimit,std::max(before+chunk,data.capacity()*2)));
            data.resize(before+chunk);
            size_t got=source->read(data.data()+before,chunk);
            data.resize(before+got);
            if(!got)sourceEnded=true;
        }
    }
    size_t len = lenBits();
    if (len < bitIndex) {
        return false;
    }
    return (len - bitIndex) >= bits;
}

bool Buffer::hasEnded() const {
    return !has(1);
}

u32 Buffer::read(size_t bits) {
    if (!has(bits)) {
        return 0;
    }
    if (bits > 0 && bits <= 24) {
        // At most four byte loads, including unaligned fields. Read only the
        // bytes covered by the field, so truncated tails never overread.
        size_t offset = bitIndex & 7;
        size_t end = offset + bits;
        const u8* p = &data[bitIndex >> 3];
        u32 word = p[0];
        size_t loaded = 8;
        if (end > 8) { word = (word << 8) | p[1]; loaded = 16; }
        if (end > 16) { word = (word << 8) | p[2]; loaded = 24; }
        if (end > 24) { word = (word << 8) | p[3]; loaded = 32; }
        bitIndex += bits;
        return (word >> (loaded - end)) & (0xffffffffu >> (32 - bits));
    }
    u32 value = 0;
    while (bits > 0) {
        u32 currentByte = data[bitIndex >> 3] & 0xff;
        size_t remaining = 8 - (bitIndex & 7);
        size_t rd = remaining < bits ? remaining : bits;
        size_t shift = remaining - rd;
        u32 mask = (u32)(0xff >> (8 - rd));
        value = (value << rd) | (u32)((currentByte & (mask << shift)) >> shift);
        bitIndex += rd;
        bits -= rd;
    }
    return value;
}

void Buffer::align() {
    bitIndex = ((bitIndex + 7) >> 3) << 3;
}

void Buffer::skip(size_t bits) {
    if (has(bits)) {
        bitIndex += bits;
    }
}

size_t Buffer::skipBytes(u8 value) {
    align();
    size_t skipped = 0;
    while (has(8) && (data[bitIndex >> 3] & 0xff) == (u32)(value & 0xff)) {
        bitIndex += 8;
        ++skipped;
    }
    return skipped;
}

s32 Buffer::nextStartCode() {
    align();
    while (has(5 << 3)) {
        size_t byteIndex = bitIndex >> 3;
        if (byteIndex + 3 < data.size() &&
            data[byteIndex] == 0x00 && data[byteIndex + 1] == 0x00 && data[byteIndex + 2] == 0x01) {
            bitIndex = (byteIndex + 4) << 3;
            return data[byteIndex + 3] & 0xff;
        }
        bitIndex += 8;
    }
    return -1;
}

s32 Buffer::findStartCode(s32 code) {
    for (;;) {
        s32 current = nextStartCode();
        if (current == code || current == -1) {
            return current;
        }
    }
}

s32 Buffer::hasStartCode(s32 code) {
    size_t previous = bitIndex;
    s32 current = findStartCode(code);
    bitIndex = previous;
    return current;
}

size_t Buffer::tell() const {
    return bitIndex >> 3;
}

void Buffer::seek(size_t bytePos) {
    bitIndex = bytePos << 3;
}

std::vector<u8> Buffer::peekBytes(size_t size) const {
    std::vector<u8> out;
    size_t start = bitIndex >> 3;
    if (start + size > data.size()) {
        return out;
    }
    out.assign(data.begin() + start, data.begin() + start + size);
    return out;
}

short Buffer::readVlc(const VlcEntry* table) {
    short index = 0;
    for (;;) {
        // VLC traversal reads one bit at a time. Avoid the general-purpose
        // read/mask loop here, while preserving its end-of-buffer behaviour.
        short bit = 0;
        if ((bitIndex >> 3) < data.size() || has(1)) {
            bit = (data[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1;
            ++bitIndex;
        }
        const VlcEntry& entry = table[index + bit];
        if (entry.next <= 0) {
            return entry.value;
        }
        index = entry.next;
    }
}

int Buffer::readVlcUint(const VlcUintEntry* table) {
    short index = 0;
    for (;;) {
        short bit = 0;
        if ((bitIndex >> 3) < data.size() || has(1)) {
            bit = (data[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1;
            ++bitIndex;
        }
        const VlcUintEntry& entry = table[index + bit];
        if (entry.next <= 0) {
            return entry.value & 0xffff;
        }
        index = entry.next;
    }
}

bool Buffer::peekNonZero(size_t bitCount) {
    if (!has(bitCount)) {
        return false;
    }
    size_t saved = bitIndex;
    u32 value = read(bitCount);
    bitIndex = saved;
    return value != 0;
}

void Buffer::discardReadBytes() {
    if(!source)return;
    size_t bytes=std::min(bitIndex>>3,data.size());
    if(bytes<32768)return;
    data.erase(data.begin(),data.begin()+bytes);bitIndex-=bytes*8;
}

bool Buffer::findFrameSync() {
    align();
    while(has(16)) {
        size_t i=bitIndex>>3;
        if(data[i]==0xff && (data[i+1]&0xfe)==0xfc) {bitIndex=((i+1)<<3)+3;return true;}
        bitIndex+=8;
    }
    return false;
}

}
}
