#ifndef NATIVE32_ERROR_H
#define NATIVE32_ERROR_H

#include <psptypes.h>
#include <string>

namespace n32 {

enum EmuErrorCode {
    EmuOk = 0,
    EmuHeaderNotFound,
    EmuDecryptionFailed,
    EmuInvalidFile,
    EmuCorruptedImage,
    EmuUnknownOpcode,
    EmuOffsetOutOfBounds,
    EmuIo,
    EmuOther
};

struct EmuError {
    EmuErrorCode code;
    std::string message;
    u32 value;

    EmuError() : code(EmuOk), value(0) {}
    EmuError(EmuErrorCode codeValue, const std::string& messageValue, u32 valueValue = 0)
        : code(codeValue), message(messageValue), value(valueValue) {}
};

}

#endif
