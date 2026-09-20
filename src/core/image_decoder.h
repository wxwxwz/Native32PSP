#ifndef NATIVE32_IMAGE_DECODER_H
#define NATIVE32_IMAGE_DECODER_H

#include "core/native32_types.h"

namespace n32 {

bool decodeImageYuv(const std::vector<u8>& data, RgbaImage* out);
bool decodeImageArgb(const std::vector<u8>& data, RgbaImage* out);
// Borrow a bounded asset span from the reader instead of copying compressed
// bytes. Optional draw metadata is gathered during decoding.
bool decodeImageYuv(const u8* data, size_t size, RgbaImage* out, ImageDrawInfo* info = 0);
bool decodeImageArgb(const u8* data, size_t size, RgbaImage* out, ImageDrawInfo* info = 0);

}

#endif
