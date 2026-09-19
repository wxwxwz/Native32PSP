#ifndef NATIVE32_DES_CONSTANTS_H
#define NATIVE32_DES_CONSTANTS_H

#include <psptypes.h>

namespace n32 {
namespace des {

extern const u8 INITIAL_MESSAGE_PERMUTATION[64];
extern const u8 FINAL_MESSAGE_PERMUTATION[64];
extern const u8 MESSAGE_SHUFFLE[48];
extern const u8 RIGHT_SUB_MESSAGE_PERMUTATION[32];
extern const u8 INITIAL_KEY_PERMUTATION[56];
extern const u8 SUB_KEY_PERMUTATION[48];
extern const u8 KEY_SHIFT_SIZES[16];
extern const u8 DES_SBOXES[512];

}
}

#endif
