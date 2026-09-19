#include "core/actions.h"

namespace n32 {

bool actionFromU32(u32 value, Action* out) {
    if (!out) {
        return false;
    }
    switch (value) {
    case 0x00: *out = ActionEnd; return true;
    case 0x04: *out = ActionNextFrame; return true;
    case 0x05: *out = ActionPreviousFrame; return true;
    case 0x06: *out = ActionPlay; return true;
    case 0x07: *out = ActionStop; return true;
    case 0x09: *out = ActionStopSounds; return true;
    case 0x0a: *out = ActionAdd; return true;
    case 0x0b: *out = ActionSubtract; return true;
    case 0x0c: *out = ActionMultiply; return true;
    case 0x0d: *out = ActionDivide; return true;
    case 0x0e: *out = ActionEquals; return true;
    case 0x0f: *out = ActionLess; return true;
    case 0x10: *out = ActionAnd; return true;
    case 0x11: *out = ActionOr; return true;
    case 0x12: *out = ActionNot; return true;
    case 0x13: *out = ActionStringEquals; return true;
    case 0x14: *out = ActionStringLength; return true;
    case 0x15: *out = ActionStringExtract; return true;
    case 0x17: *out = ActionPop; return true;
    case 0x18: *out = ActionToInteger; return true;
    case 0x1c: *out = ActionGetVariable; return true;
    case 0x1d: *out = ActionSetVariable; return true;
    case 0x20: *out = ActionSetTarget2; return true;
    case 0x21: *out = ActionStringAdd; return true;
    case 0x22: *out = ActionGetProperty; return true;
    case 0x23: *out = ActionSetProperty; return true;
    case 0x24: *out = ActionCloneSprite; return true;
    case 0x25: *out = ActionRemoveSprite; return true;
    case 0x26: *out = ActionTrace; return true;
    case 0x27: *out = ActionStartDrag; return true;
    case 0x28: *out = ActionEndDrag; return true;
    case 0x29: *out = ActionStringLess; return true;
    case 0x30: *out = ActionRandomNumber; return true;
    case 0x31: *out = ActionMBStringLength; return true;
    case 0x32: *out = ActionCharToAscii; return true;
    case 0x33: *out = ActionAsciiToChar; return true;
    case 0x34: *out = ActionGetTime; return true;
    case 0x35: *out = ActionMBStringExtract; return true;
    case 0x36: *out = ActionMBCharToAscii; return true;
    case 0x37: *out = ActionMBAsciiToChar; return true;
    case 0x81: *out = ActionGotoFrame; return true;
    case 0x8a: *out = ActionWaitForFrame; return true;
    case 0x8b: *out = ActionSetTarget; return true;
    case 0x8c: *out = ActionGotoLabel; return true;
    case 0x8d: *out = ActionWaitForFrame2; return true;
    case 0x96: *out = ActionPush; return true;
    case 0x99: *out = ActionJump; return true;
    case 0x9a: *out = ActionGetUrl2; return true;
    case 0x9d: *out = ActionIf; return true;
    case 0x9e: *out = ActionCall; return true;
    case 0x9f: *out = ActionGotoFrame2; return true;
    default: return false;
    }
}

}
