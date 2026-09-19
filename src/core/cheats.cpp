#include "core/cheats.h"
#include "core/content_loader.h"
#include <algorithm>
#include <ctype.h>
#include <stdlib.h>

namespace n32 {

static std::string lowerCheat(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

static bool parseSpriteField(const std::string& value, SpriteField* out) {
    std::string field = lowerCheat(trimString(value));
    if (field == "x") *out = SpriteFieldX;
    else if (field == "y") *out = SpriteFieldY;
    else if (field == "depth") *out = SpriteFieldDepth;
    else if (field == "frame" || field == "currentframe" || field == "current_frame") *out = SpriteFieldFrame;
    else if (field == "visible") *out = SpriteFieldVisible;
    else if (field == "playing") *out = SpriteFieldPlaying;
    else return false;
    return true;
}

static bool parseFrameField(const std::string& value, FrameField* out) {
    std::string field = lowerCheat(trimString(value));
    if (field == "goto" || field == "current" || field == "currentframe" || field == "current_frame") *out = FrameFieldGoto;
    else if (field == "playing") *out = FrameFieldPlaying;
    else return false;
    return true;
}

CheatManager::CheatManager() {
}

void CheatManager::clear() {
    slots.clear();
}

CheatParseError CheatManager::setSlot(u32 index, bool enabled, const std::string& code) {
    std::string trimmed = trimString(code);
    if (trimmed.empty()) {
        slots.erase(index);
        return CheatParseOk;
    }
    CheatRule rule;
    CheatParseError err = parseCheatRule(trimmed, &rule);
    if (err != CheatParseOk) {
        return err;
    }
    CheatSlot slot;
    slot.enabled = enabled;
    slot.code = trimmed;
    slot.rule = rule;
    slots[index] = slot;
    return CheatParseOk;
}

CheatParseError CheatManager::addCode(const std::string& code, u32* outIndex) {
    u32 index = slots.empty() ? 0 : (slots.rbegin()->first + 1);
    CheatParseError err = setSlot(index, true, code);
    if (err == CheatParseOk && outIndex) {
        *outIndex = index;
    }
    return err;
}

size_t CheatManager::len() const {
    return slots.size();
}

bool CheatManager::isEmpty() const {
    return slots.empty();
}

void CheatManager::apply(ActionVM* vm, SpriteSystem* sprites, FramePlayer* frame) const {
    if (!vm || !sprites || !frame) {
        return;
    }
    for (std::map<u32, CheatSlot>::const_iterator it = slots.begin(); it != slots.end(); ++it) {
        const CheatSlot& slot = it->second;
        if (!slot.enabled) {
            continue;
        }
        const CheatRule& rule = slot.rule;
        if (rule.kind == CheatRuleVariable) {
            vm->vars[rule.name] = rule.value;
        } else if (rule.kind == CheatRuleSprite) {
            MovieState* movie = sprites->getMutable(rule.name);
            if (!movie) {
                continue;
            }
            switch (rule.spriteField) {
            case SpriteFieldX: movie->x = parseI16(rule.value); break;
            case SpriteFieldY: movie->y = parseI16(rule.value); break;
            case SpriteFieldDepth: movie->depth = parseU16(rule.value); break;
            case SpriteFieldFrame:
                movie->hasNextFrame = true;
                movie->nextFrame = (s32)parseSize(rule.value);
                break;
            case SpriteFieldVisible: movie->visible = parseBool(rule.value); break;
            case SpriteFieldPlaying: movie->playing = parseBool(rule.value); break;
            }
        } else if (rule.kind == CheatRuleFrame) {
            if (rule.frameField == FrameFieldGoto) {
                frame->gotoFrame(parseU32(rule.value), frame->playing);
            } else {
                frame->playing = parseBool(rule.value);
            }
        }
    }
}

CheatParseError parseCheatRule(const std::string& code, CheatRule* out) {
    if (!out) {
        return CheatParseUnknownTarget;
    }
    std::string input = trimString(code);
    if (input.empty()) {
        return CheatParseEmpty;
    }
    size_t eq = input.find('=');
    if (eq == std::string::npos) {
        return CheatParseMissingValue;
    }
    std::string target = trimString(input.substr(0, eq));
    std::string value = trimString(input.substr(eq + 1));

    if (target.compare(0, 4, "var:") == 0) {
        std::string name = trimString(target.substr(4));
        if (name.empty()) {
            return CheatParseUnknownTarget;
        }
        out->kind = CheatRuleVariable;
        out->name = lowerCheat(name);
        out->value = value;
        return CheatParseOk;
    }

    bool spritePrefix = target.compare(0, 7, "sprite:") == 0;
    bool moviePrefix = target.compare(0, 6, "movie:") == 0;
    if (spritePrefix || moviePrefix) {
        std::string spec = target.substr(spritePrefix ? 7 : 6);
        size_t dot = spec.find_last_of('.');
        if (dot == std::string::npos) {
            return CheatParseInvalidSpriteTarget;
        }
        std::string name = trimString(spec.substr(0, dot));
        if (name.empty()) {
            return CheatParseInvalidSpriteTarget;
        }
        SpriteField field;
        if (!parseSpriteField(spec.substr(dot + 1), &field)) {
            return CheatParseInvalidSpriteTarget;
        }
        out->kind = CheatRuleSprite;
        out->name = name;
        out->spriteField = field;
        out->value = value;
        return CheatParseOk;
    }

    if (target.compare(0, 6, "frame:") == 0) {
        FrameField field;
        if (!parseFrameField(target.substr(6), &field)) {
            return CheatParseInvalidFrameTarget;
        }
        out->kind = CheatRuleFrame;
        out->frameField = field;
        out->value = value;
        return CheatParseOk;
    }

    return CheatParseUnknownTarget;
}

bool parseBool(const std::string& value) {
    std::string lower = lowerCheat(trimString(value));
    if (lower.empty() || lower == "0" || lower == "false" || lower == "off" || lower == "no") {
        return false;
    }
    if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        return true;
    }
    return parseI64(value) != 0;
}

s64 parseI64(const std::string& value) {
    return (s64)strtod(trimString(value).c_str(), 0);
}

s16 parseI16(const std::string& value) {
    s64 v = parseI64(value);
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    return (s16)v;
}

u16 parseU16(const std::string& value) {
    s64 v = parseI64(value);
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (u16)v;
}

u32 parseU32(const std::string& value) {
    s64 v = parseI64(value);
    if (v < 0) v = 0;
    return (u32)v;
}

size_t parseSize(const std::string& value) {
    s64 v = parseI64(value);
    return v < 0 ? 0 : (size_t)v;
}

}
