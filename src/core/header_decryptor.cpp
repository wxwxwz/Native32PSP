#include "core/header_decryptor.h"
#include "core/des_constants.h"
#include <string.h>

namespace n32 {

using namespace des;

static void expandBits(const u8* data, size_t count, std::vector<u8>* out) {
    out->assign(count, 0);
    for (size_t i = 0; i < count; ++i) {
        (*out)[i] = (data[i >> 3] >> (i & 7)) & 1;
    }
}

static void compressBits(const std::vector<u8>& data, size_t count, std::vector<u8>* out) {
    out->assign(count / 8, 0);
    for (size_t i = 0; i < count; ++i) {
        (*out)[i >> 3] |= data[i] << (i & 7);
    }
}

static void doShuffle(std::vector<u8>* dst, const std::vector<u8>& src, const u8* table, size_t count, size_t offset) {
    if (dst->size() < offset + count) {
        dst->resize(offset + count);
    }
    for (size_t i = 0; i < count; ++i) {
        (*dst)[i + offset] = src[table[i] - 1];
    }
}

static void sliceAndDice(std::vector<u8>* src, size_t count, size_t splitpoint, size_t offset) {
    std::vector<u8> temp(splitpoint, 0);
    for (size_t i = 0; i < splitpoint; ++i) {
        temp[i] = (*src)[offset + i];
    }
    for (size_t i = 0; i < count - splitpoint; ++i) {
        (*src)[offset + i] = (*src)[offset + i + splitpoint];
    }
    for (size_t i = 0; i < splitpoint; ++i) {
        (*src)[offset + i + (count - splitpoint)] = temp[i];
    }
}

static void expandKey(const u8* key, std::vector<u8>* out) {
    std::vector<u8> raw;
    expandBits(key, 0x40, &raw);
    std::vector<u8> keyBits(0x38, 0);
    for (size_t i = 0; i < 0x38; ++i) {
        keyBits[i] = raw[INITIAL_KEY_PERMUTATION[i] - 1];
    }

    out->assign(0x30 * 0x10, 0);
    for (size_t i = 0; i < 0x10; ++i) {
        size_t splitpoint = KEY_SHIFT_SIZES[i];
        sliceAndDice(&keyBits, 0x1c, splitpoint, 0);
        sliceAndDice(&keyBits, 0x1c, splitpoint, 0x1c);
        doShuffle(out, keyBits, SUB_KEY_PERMUTATION, 0x30, i * 0x30);
    }
}

static void doSbox(std::vector<u8>* data, const std::vector<u8>& key, size_t keyOffset) {
    for (size_t i = 0; i < 8; ++i) {
        size_t k = keyOffset + i * 6;
        size_t idx = (i * 4 + key[k + 5] + key[k] * 2) * 0x10
            + (key[k + 4] + key[k + 1] * 8 + key[k + 2] * 4 + key[k + 3] * 2);
        std::vector<u8> bits;
        expandBits(&DES_SBOXES[idx], 4, &bits);
        for (size_t j = 0; j < 4; ++j) {
            (*data)[i * 4 + j] = bits[j];
        }
    }
}

static void processIteration(std::vector<u8>* data, const std::vector<u8>& key, size_t keyOffset) {
    std::vector<u8> iterTemp(0x30, 0);
    doShuffle(&iterTemp, *data, MESSAGE_SHUFFLE, 0x30, 0);
    for (size_t i = 0; i < 0x30; ++i) {
        iterTemp[i] ^= key[keyOffset + i];
    }
    doSbox(data, iterTemp, 0);
    std::vector<u8> copy = *data;
    doShuffle(data, copy, RIGHT_SUB_MESSAGE_PERMUTATION, 0x20, 0);
}

static void decryptChunk(const u8* src, const std::vector<u8>& expandedKey, std::vector<u8>* out) {
    std::vector<u8> raw;
    expandBits(src, 0x40, &raw);
    std::vector<u8> data(0x40, 0);
    for (size_t i = 0; i < 0x40; ++i) {
        data[i] = raw[INITIAL_MESSAGE_PERMUTATION[i] - 1];
    }

    for (int i = 0x2d0; i >= 0; i -= 0x30) {
        u8 temp[0x20];
        for (size_t j = 0; j < 0x20; ++j) {
            temp[j] = data[j];
        }
        processIteration(&data, expandedKey, (size_t)i);
        for (size_t j = 0; j < 0x20; ++j) {
            data[j] ^= data[0x20 + j];
        }
        for (size_t j = 0; j < 0x20; ++j) {
            data[0x20 + j] = temp[j];
        }
    }

    std::vector<u8> finalData = data;
    for (size_t i = 0; i < 0x40; ++i) {
        data[i] = finalData[FINAL_MESSAGE_PERMUTATION[i] - 1];
    }
    compressBits(data, 0x40, out);
}

static void doDecrypt(const u8* data, size_t size, const u8* key, std::vector<u8>* out) {
    std::vector<u8> expandedKey;
    expandKey(key, &expandedKey);
    out->clear();
    for (size_t i = 0; i < size / 8; ++i) {
        std::vector<u8> chunk;
        decryptChunk(data + i * 8, expandedKey, &chunk);
        out->insert(out->end(), chunk.begin(), chunk.end());
    }
}

bool decryptHeader(const u8* data, size_t size, std::vector<u8>* out) {
    if (!data || !out) {
        return false;
    }
    static const char* keys[] = { "11111111", "22222222", "aaaaaaaa", "bbbbbbbb", "aber3801" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        std::vector<u8> decrypted;
        doDecrypt(data, size, (const u8*)keys[i], &decrypted);
        if (decrypted.size() >= 8 && memcmp(&decrypted[4], "8202", 4) == 0) {
            *out = decrypted;
            return true;
        }
    }
    return false;
}

}
