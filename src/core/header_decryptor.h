#ifndef NATIVE32_HEADER_DECRYPTOR_H
#define NATIVE32_HEADER_DECRYPTOR_H

#include <psptypes.h>
#include <vector>

namespace n32 {

bool decryptHeader(const u8* data, size_t size, std::vector<u8>* out);

}

#endif
