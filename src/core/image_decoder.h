#ifndef NATIVE32_IMAGE_DECODER_H
#define NATIVE32_IMAGE_DECODER_H

#include "core/native32_types.h"

namespace n32 {

bool decodeImageYuv(const std::vector<u8>& data, RgbaImage* out);
bool decodeImageArgb(const std::vector<u8>& data, RgbaImage* out);

}

#endif
