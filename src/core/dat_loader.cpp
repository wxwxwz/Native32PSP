#include "core/dat_loader.h"
#include "core/endian.h"
#include "core/image_decoder.h"
#include <string.h>

namespace n32 {

static const size_t NAME_OFFSET_PTR = 0x38;
static const size_t PREVIEW_OFFSET_PTR = 0x58;

DatImage datImageFromFlag(const std::string& flag) {
    return flag == "J" ? DatImagePreview : DatImageName;
}

bool decodeDatImage(const std::vector<u8>& data, Colorspace colorspace, DatImage which, RgbaImage* out) {
    size_t ptr = which == DatImagePreview ? PREVIEW_OFFSET_PTR : NAME_OFFSET_PTR;
    if (!out || data.size() < ptr + 4 || memcmp(&data[0], "INFO", 4) != 0) {
        return false;
    }
    size_t offset = read_u32_le(&data[0], ptr);
    if (offset + 8 > data.size()) {
        return false;
    }
    std::vector<u8> block(data.begin() + offset, data.end());
    return colorspace == ColorspaceArgb ? decodeImageArgb(block, out) : decodeImageYuv(block, out);
}

}
