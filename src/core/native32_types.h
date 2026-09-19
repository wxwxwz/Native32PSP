#ifndef NATIVE32_TYPES_H
#define NATIVE32_TYPES_H

#include <psptypes.h>
#include <string>
#include <vector>

namespace n32 {

enum ObjectType {
    ObjectImage = 1,
    ObjectMovie = 2,
    ObjectButton = 3,
    ObjectAction = 4,
    ObjectSound = 5
};

enum Colorspace {
    ColorspaceYuv,
    ColorspaceArgb
};

enum AudioFormat {
    AudioMp3,
    AudioRaw
};

enum Action {
    ActionEnd = 0x00,
    ActionNextFrame = 0x04,
    ActionPreviousFrame = 0x05,
    ActionPlay = 0x06,
    ActionStop = 0x07,
    ActionStopSounds = 0x09,
    ActionAdd = 0x0a,
    ActionSubtract = 0x0b,
    ActionMultiply = 0x0c,
    ActionDivide = 0x0d,
    ActionEquals = 0x0e,
    ActionLess = 0x0f,
    ActionAnd = 0x10,
    ActionOr = 0x11,
    ActionNot = 0x12,
    ActionStringEquals = 0x13,
    ActionStringLength = 0x14,
    ActionStringExtract = 0x15,
    ActionPop = 0x17,
    ActionToInteger = 0x18,
    ActionGetVariable = 0x1c,
    ActionSetVariable = 0x1d,
    ActionSetTarget2 = 0x20,
    ActionStringAdd = 0x21,
    ActionGetProperty = 0x22,
    ActionSetProperty = 0x23,
    ActionCloneSprite = 0x24,
    ActionRemoveSprite = 0x25,
    ActionTrace = 0x26,
    ActionStartDrag = 0x27,
    ActionEndDrag = 0x28,
    ActionStringLess = 0x29,
    ActionRandomNumber = 0x30,
    ActionMBStringLength = 0x31,
    ActionCharToAscii = 0x32,
    ActionAsciiToChar = 0x33,
    ActionGetTime = 0x34,
    ActionMBStringExtract = 0x35,
    ActionMBCharToAscii = 0x36,
    ActionMBAsciiToChar = 0x37,
    ActionGotoFrame = 0x81,
    ActionWaitForFrame = 0x8a,
    ActionSetTarget = 0x8b,
    ActionGotoLabel = 0x8c,
    ActionWaitForFrame2 = 0x8d,
    ActionPush = 0x96,
    ActionJump = 0x99,
    ActionGetUrl2 = 0x9a,
    ActionIf = 0x9d,
    ActionCall = 0x9e,
    ActionGotoFrame2 = 0x9f
};

struct RgbaImage {
    u32 width;
    u32 height;
    std::vector<u32> pixels;
};

struct FrameObject {
    ObjectType type;
    u16 index;
    s16 x;
    s16 y;
    u16 depth;
    std::string name;
    bool hasName;

    FrameObject() : type(ObjectImage), index(0), x(0), y(0), depth(0), hasName(false) {}
};

struct MovieFrame {
    u16 image;
    s16 x;
    s16 y;
    u16 action;
    u16 sound;
    u16 reserved;

    MovieFrame() : image(0), x(0), y(0), action(0), sound(0), reserved(0) {}
};

struct SoundData {
    AudioFormat format;
    std::vector<u8> data;
};

struct ActionPayload {
    bool hasPayload;
    bool isInteger;
    s16 integer;
    std::string text;

    ActionPayload() : hasPayload(false), isInteger(false), integer(0) {}
};

struct ActionEntry {
    Action action;
    u32 opcode;
    ActionPayload payload;

    ActionEntry() : action(ActionEnd), opcode(0) {}
};

struct ButtonEvent {
    u16 keycode;
    u16 event;

    ButtonEvent() : keycode(0), event(0) {}
    ButtonEvent(u16 keycodeValue, u16 eventValue) : keycode(keycodeValue), event(eventValue) {}
};

ObjectType objectTypeFromU16(u16 value, bool* ok);

}

#endif
