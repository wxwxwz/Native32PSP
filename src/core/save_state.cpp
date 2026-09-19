#include "core/save_state.h"
#include "core/endian.h"
#include <string.h>

namespace n32 {

static const u8 MAGIC[8] = { 'N', '3', '2', 'S', 'T', 'A', 'T', 'E' };
static const u32 VERSION = 2;

u32 crc32(const u8* data, size_t size) {
    u32 crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void writeU32Le(std::vector<u8>* out, size_t offset, u32 value) {
    (*out)[offset] = (u8)(value & 0xff);
    (*out)[offset + 1] = (u8)((value >> 8) & 0xff);
    (*out)[offset + 2] = (u8)((value >> 16) & 0xff);
    (*out)[offset + 3] = (u8)((value >> 24) & 0xff);
}

bool encodeRawState(const std::vector<u8>& payload, std::vector<u8>* output) {
    if (!output || payload.size() > SerializedStateSize - SaveStateHeaderSize) {
        return false;
    }
    output->assign(SerializedStateSize, 0);
    memcpy(&(*output)[0], MAGIC, 8);
    writeU32Le(output, 8, VERSION);
    writeU32Le(output, 12, (u32)payload.size());
    writeU32Le(output, 16, crc32(payload.empty() ? 0 : &payload[0], payload.size()));
    if (!payload.empty()) {
        memcpy(&(*output)[SaveStateHeaderSize], &payload[0], payload.size());
    }
    return true;
}

bool decodeRawState(const std::vector<u8>& input, std::vector<u8>* payload) {
    if (!payload || input.size() < SaveStateHeaderSize || memcmp(&input[0], MAGIC, 8) != 0) {
        return false;
    }
    if (read_u32_le(&input[0], 8) != VERSION) {
        return false;
    }
    size_t payloadSize = read_u32_le(&input[0], 12);
    u32 expectedCrc = read_u32_le(&input[0], 16);
    if (payloadSize > SerializedStateSize - SaveStateHeaderSize || SaveStateHeaderSize + payloadSize > input.size()) {
        return false;
    }
    const u8* begin = payloadSize == 0 ? 0 : &input[SaveStateHeaderSize];
    if (crc32(begin, payloadSize) != expectedCrc) {
        return false;
    }
    payload->assign(input.begin() + SaveStateHeaderSize, input.begin() + SaveStateHeaderSize + payloadSize);
    return true;
}

size_t serializedSize() {
    return SerializedStateSize;
}

}
