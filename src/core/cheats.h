#ifndef NATIVE32_CHEATS_H
#define NATIVE32_CHEATS_H

#include "core/action_vm.h"
#include "core/frame_player.h"
#include "core/sprite_system.h"

namespace n32 {

enum CheatParseError {
    CheatParseOk = 0,
    CheatParseEmpty,
    CheatParseMissingValue,
    CheatParseUnknownTarget,
    CheatParseInvalidSpriteTarget,
    CheatParseInvalidFrameTarget
};

enum CheatRuleKind {
    CheatRuleVariable,
    CheatRuleSprite,
    CheatRuleFrame
};

enum SpriteField {
    SpriteFieldX,
    SpriteFieldY,
    SpriteFieldDepth,
    SpriteFieldFrame,
    SpriteFieldVisible,
    SpriteFieldPlaying
};

enum FrameField {
    FrameFieldGoto,
    FrameFieldPlaying
};

struct CheatRule {
    CheatRuleKind kind;
    std::string name;
    std::string value;
    SpriteField spriteField;
    FrameField frameField;

    CheatRule() : kind(CheatRuleVariable), spriteField(SpriteFieldX), frameField(FrameFieldGoto) {}
};

struct CheatSlot {
    bool enabled;
    std::string code;
    CheatRule rule;

    CheatSlot() : enabled(false) {}
};

class CheatManager {
public:
    CheatManager();

    void clear();
    CheatParseError setSlot(u32 index, bool enabled, const std::string& code);
    CheatParseError addCode(const std::string& code, u32* outIndex);
    size_t len() const;
    bool isEmpty() const;
    void apply(ActionVM* vm, SpriteSystem* sprites, FramePlayer* frame) const;

    std::map<u32, CheatSlot> slots;
};

CheatParseError parseCheatRule(const std::string& code, CheatRule* out);
bool parseBool(const std::string& value);
s64 parseI64(const std::string& value);
s16 parseI16(const std::string& value);
u16 parseU16(const std::string& value);
u32 parseU32(const std::string& value);
size_t parseSize(const std::string& value);

}

#endif
