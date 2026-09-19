#include "core/action_vm.h"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

namespace n32 {

bool actionPropFromU32(u32 value, ActionProp* out) {
    if (!out) {
        return false;
    }
    switch (value) {
    case 0: *out = ActionPropX; return true;
    case 1: *out = ActionPropY; return true;
    case 2: *out = ActionPropXScale; return true;
    case 3: *out = ActionPropYScale; return true;
    case 4: *out = ActionPropCurrentFrame; return true;
    case 5: *out = ActionPropTotalFrames; return true;
    case 6: *out = ActionPropAlpha; return true;
    case 7: *out = ActionPropVisible; return true;
    case 8: *out = ActionPropWidth; return true;
    case 9: *out = ActionPropHeight; return true;
    case 13: *out = ActionPropName; return true;
    default: return false;
    }
}

double strToFloat(const std::string& value) {
    if (value.empty()) {
        return 0.0;
    }
    char* end = 0;
    double result = strtod(value.c_str(), &end);
    return end == value.c_str() ? 0.0 : result;
}

s64 strToInt(const std::string& value) {
    return (s64)strToFloat(value);
}

std::string lowerString(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

std::string numberToString(double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", value);
    return std::string(buf);
}

std::string intToString(s64 value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)value);
    return std::string(buf);
}

static std::string popOrDefault(std::vector<std::string>* stack) {
    if (!stack || stack->empty()) {
        return std::string();
    }
    std::string value = stack->back();
    stack->pop_back();
    return value;
}

ActionVM::ActionVM() : rngState(0x9e3779b97f4a7c15ULL) {
}

u32 ActionVM::randomBelow(u32 upper) {
    if (upper == 0) {
        return 0;
    }
    rngState += 0x9e3779b97f4a7c15ULL;
    u64 value = rngState;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return (u32)((value ^ (value >> 31)) % upper);
}

void ActionVM::run(Native32Reader* reader, VmHost* host, u32 index, const std::string& target) {
    if (!reader || !host || index == 0) {
        return;
    }

    u32 pc = index;
    std::vector<std::string> stack;
    std::string currentTarget = target;

    while (true) {
        u32 npc = pc + 1;
        ActionEntry entry;
        if (!reader->getAction(pc, &entry)) {
            break;
        }

        switch (entry.action) {
        case ActionEnd:
            return;

        case ActionPush:
            if (entry.payload.hasPayload) {
                stack.push_back(entry.payload.isInteger ? intToString(entry.payload.integer) : entry.payload.text);
            }
            break;

        case ActionPop:
            popOrDefault(&stack);
            break;

        case ActionSetVariable: {
            std::string val = popOrDefault(&stack);
            std::string var = popOrDefault(&stack);
            vars[lowerString(var)] = val;
            break;
        }

        case ActionGetVariable: {
            std::string name = lowerString(popOrDefault(&stack));
            std::map<std::string, std::string>::const_iterator it = vars.find(name);
            stack.push_back(it == vars.end() ? std::string() : it->second);
            break;
        }

        case ActionAdd: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(numberToString(strToFloat(a) + strToFloat(b)));
            break;
        }

        case ActionSubtract: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(numberToString(strToFloat(a) - strToFloat(b)));
            break;
        }

        case ActionMultiply: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(numberToString(strToFloat(a) * strToFloat(b)));
            break;
        }

        case ActionDivide: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            double bv = strToFloat(b);
            stack.push_back(numberToString(bv != 0.0 ? strToFloat(a) / bv : 0.0));
            break;
        }

        case ActionEquals: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToFloat(a) == strToFloat(b) ? 1 : 0));
            break;
        }

        case ActionLess: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToFloat(a) < strToFloat(b) ? 1 : 0));
            break;
        }

        case ActionAnd: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToInt(a) != 0 && strToInt(b) != 0 ? 1 : 0));
            break;
        }

        case ActionOr: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToInt(a) != 0 || strToInt(b) != 0 ? 1 : 0));
            break;
        }

        case ActionNot: {
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToInt(a) == 0 ? 1 : 0));
            break;
        }

        case ActionStringEquals: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(a == b ? 1 : 0));
            break;
        }

        case ActionStringAdd: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(a + b);
            break;
        }

        case ActionStringLess: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(a < b ? 1 : 0));
            break;
        }

        case ActionStringLength: {
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString((s64)a.size()));
            break;
        }

        case ActionStringExtract: {
            std::string len = popOrDefault(&stack);
            std::string start = popOrDefault(&stack);
            std::string s = popOrDefault(&stack);
            s64 startIndex = strToInt(start) - 1;
            if (startIndex < 0) {
                startIndex = 0;
            }
            size_t begin = (size_t)startIndex;
            size_t count = (size_t)std::max<s64>(0, strToInt(len));
            stack.push_back(begin < s.size() ? s.substr(begin, count) : std::string());
            break;
        }

        case ActionToInteger: {
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(strToInt(a)));
            break;
        }

        case ActionCharToAscii: {
            std::string a = popOrDefault(&stack);
            stack.push_back(intToString(a.empty() ? 0 : (unsigned char)a[0]));
            break;
        }

        case ActionAsciiToChar: {
            std::string a = popOrDefault(&stack);
            char ch = (char)(strToInt(a) & 0xff);
            stack.push_back(std::string(1, ch));
            break;
        }

        case ActionJump:
            if (entry.payload.hasPayload && entry.payload.isInteger) {
                s32 offset = entry.payload.integer;
                pc = offset >= 0 ? (u32)((s32)pc + offset + 1) : (u32)((s32)pc + offset);
                continue;
            }
            break;

        case ActionIf: {
            std::string cond = popOrDefault(&stack);
            if (strToInt(cond) != 0 && entry.payload.hasPayload && entry.payload.isInteger) {
                s32 offset = entry.payload.integer;
                pc = offset >= 0 ? (u32)((s32)pc + offset + 1) : (u32)((s32)pc + offset);
                continue;
            }
            break;
        }

        case ActionStop:
            host->stop(currentTarget);
            break;

        case ActionPlay:
            host->play(currentTarget);
            break;

        case ActionStopSounds:
            host->stopSounds(currentTarget);
            break;

        case ActionNextFrame: {
            u32 frame = host->getFrame(currentTarget);
            host->gotoFrame(currentTarget, frame + 1, false);
            break;
        }

        case ActionPreviousFrame: {
            u32 frame = host->getFrame(currentTarget);
            host->gotoFrame(currentTarget, frame == 0 ? 0 : frame - 1, false);
            break;
        }

        case ActionGotoFrame:
            if (entry.payload.hasPayload && entry.payload.isInteger) {
                host->gotoFrame(currentTarget, (u32)entry.payload.integer + 1, false);
            }
            break;

        case ActionGotoFrame2: {
            u32 frame = (u32)strToFloat(popOrDefault(&stack));
            bool playing = entry.payload.hasPayload && entry.payload.isInteger && (entry.payload.integer & 1) != 0;
            host->gotoFrame(currentTarget, frame, playing);
            break;
        }

        case ActionSetTarget:
            if (entry.payload.hasPayload && !entry.payload.isInteger) {
                currentTarget = entry.payload.text;
            }
            break;

        case ActionSetTarget2:
            currentTarget = popOrDefault(&stack);
            break;

        case ActionSetProperty: {
            std::string value = popOrDefault(&stack);
            std::string propValue = popOrDefault(&stack);
            std::string targetName = popOrDefault(&stack);
            ActionProp prop;
            if (actionPropFromU32((u32)strToInt(propValue), &prop)) {
                host->setProperty(targetName, prop, value);
            }
            break;
        }

        case ActionGetProperty: {
            std::string propValue = popOrDefault(&stack);
            std::string targetName = popOrDefault(&stack);
            ActionProp prop;
            stack.push_back(actionPropFromU32((u32)strToInt(propValue), &prop) ? host->getProperty(targetName, prop) : "0");
            break;
        }

        case ActionCloneSprite: {
            std::string depth = popOrDefault(&stack);
            std::string dest = popOrDefault(&stack);
            std::string src = popOrDefault(&stack);
            host->cloneSprite(src, dest, (s32)strToInt(depth));
            break;
        }

        case ActionRemoveSprite:
            host->removeSprite(popOrDefault(&stack));
            break;

        case ActionCall:
            host->call((u32)strToInt(popOrDefault(&stack)));
            break;

        case ActionRandomNumber: {
            u32 upper = (u32)strToInt(popOrDefault(&stack));
            stack.push_back(intToString(randomBelow(upper)));
            break;
        }

        case ActionGetTime:
            stack.push_back(intToString(host->getTime()));
            break;

        case ActionGetUrl2: {
            std::string targetValue = popOrDefault(&stack);
            std::string url = popOrDefault(&stack);
            host->getUrl(url, targetValue);
            break;
        }

        case ActionTrace:
            popOrDefault(&stack);
            break;

        default:
            break;
        }

        pc = npc;
    }
}

}
