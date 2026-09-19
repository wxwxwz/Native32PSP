#ifndef NATIVE32_ENDIAN_H
#define NATIVE32_ENDIAN_H

#include <psptypes.h>
#include <stddef.h>

namespace n32 {

inline u16 read_u16_le(const u8* data, size_t offset) {
    return (u16)data[offset] | ((u16)data[offset + 1] << 8);
}

inline s16 read_i16_le(const u8* data, size_t offset) {
    return (s16)read_u16_le(data, offset);
}

inline u32 read_u32_le(const u8* data, size_t offset) {
    return (u32)data[offset]
        | ((u32)data[offset + 1] << 8)
        | ((u32)data[offset + 2] << 16)
        | ((u32)data[offset + 3] << 24);
}

}

#endif
