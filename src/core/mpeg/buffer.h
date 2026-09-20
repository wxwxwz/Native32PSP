#ifndef NATIVE32_MPEG_BUFFER_H
#define NATIVE32_MPEG_BUFFER_H

#include <psptypes.h>
#include <vector>
#include <memory>

namespace n32 {
namespace mpeg {

struct VlcEntry {
    short next;
    short value;

    VlcEntry() : next(0), value(0) {}
    VlcEntry(int nextValue, int valueValue) : next((short)nextValue), value((short)valueValue) {}
};

struct VlcUintEntry {
    short next;
    int value;

    VlcUintEntry() : next(0), value(0) {}
    VlcUintEntry(int nextValue, int valueValue) : next((short)nextValue), value(valueValue & 0xffff) {}
};

// A bounded first-byte lookup for an immutable VLC tree. Long codes continue
// at the stored tree node; entries retain the exact number of consumed bits.
struct VlcUintPrefix {
    struct Entry {
        short next;
        unsigned short value;
        unsigned char bits;
    };
    const VlcUintEntry* table;
    Entry entries[256];
    explicit VlcUintPrefix(const VlcUintEntry* tree);
};

// MSB-first bit reader for MPEG streams. Faithful port of the Java MpegBuffer
// reference implementation.
class ByteSource {
public:
    virtual ~ByteSource() {}
    virtual size_t read(u8* destination,size_t capacity)=0;
};
class Buffer {
public:
    Buffer();
    explicit Buffer(std::vector<u8> bytes);
    explicit Buffer(std::shared_ptr<ByteSource> source);

    size_t lenBits() const;
    bool has(size_t bits) const;
    bool hasEnded() const;
    u32 read(size_t bits);
    void align();
    void skip(size_t bits);
    size_t skipBytes(u8 value);
    s32 nextStartCode();
    s32 findStartCode(s32 code);
    s32 hasStartCode(s32 code);
    size_t tell() const;
    void seek(size_t bytePos);
    std::vector<u8> peekBytes(size_t size) const;
    short readVlc(const VlcEntry* table);
    int readVlcUint(const VlcUintEntry* table);
    int readVlcUint(const VlcUintPrefix& prefix);
    bool peekNonZero(size_t bitCount);
    void discardReadBytes();
    bool findFrameSync();

    mutable std::vector<u8> data;
    std::shared_ptr<ByteSource> source;
    mutable bool sourceEnded=false;
    static const size_t streamLimit=512*1024;
    size_t bitIndex;

private:
    int readVlcUintAt(const VlcUintEntry* table, short index);
};

}
}

#endif
