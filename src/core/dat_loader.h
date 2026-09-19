#ifndef NATIVE32_DAT_LOADER_H
#define NATIVE32_DAT_LOADER_H

#include "core/native32_types.h"

namespace n32 {

enum DatImage {
    DatImageName,
    DatImagePreview
};

DatImage datImageFromFlag(const std::string& flag);
bool decodeDatImage(const std::vector<u8>& data, Colorspace colorspace, DatImage which, RgbaImage* out);

}

#endif
