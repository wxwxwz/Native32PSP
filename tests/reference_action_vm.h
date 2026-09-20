#ifndef N32_REFERENCE_ACTION_VM_H
#define N32_REFERENCE_ACTION_VM_H
#include "core/action_vm.h"
// Frozen 20260920.0125 interpreter, used only as a differential test oracle.
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#if defined(PSP) || defined(N32_TEST_CORE_PROFILE)
#include <pspkernel.h>
#endif

namespace n32_vm_reference {
using namespace n32;
class ActionVM {
public:
    ActionVM();

    void run(Native32Reader* reader, VmHost* host, u32 index, const std::string& target);
    u32 randomBelow(u32 upper);
    void resetProfile();

    std::map<std::string, std::string> vars;
    u64 rngState;
    VmTickProfile profile;

private:
    struct RunProfileScope;
    // A copied/reloaded VM must never inherit pointers to another run's stack.
    // This leaves the existing automatic copy/move of script state intact.
    struct ProfileNesting {
        RunProfileScope* active;
        ProfileNesting() : active(0) {}
        ProfileNesting(const ProfileNesting&) : active(0) {}
        ProfileNesting& operator=(const ProfileNesting&) { active = 0; return *this; }
    } profileNesting;
};


static u32 vmProfileClock() {
#if defined(PSP) || defined(N32_TEST_CORE_PROFILE)
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}

struct ActionVM::RunProfileScope {
    ActionVM& vm;
    RunProfileScope* previous;
    u32 depth, begin, action;
    char target[48];

    RunProfileScope(ActionVM& value, u32 entry, const std::string& entryTarget)
        : vm(value), previous(value.profileNesting.active),
          depth(previous ? previous->depth + 1 : 1), begin(0), action(entry) {
        vm.profileNesting.active = this;
        ++vm.profile.calls;
        vm.profile.maxDepth = std::max(vm.profile.maxDepth, depth);
        if (!previous) {
            begin = vmProfileClock();
            // Snapshot the entry target before callbacks can rename/remove it.
            const size_t length = std::min(entryTarget.size(), sizeof(target) - 1);
            if (length) std::memcpy(target, entryTarget.data(), length);
            for (size_t i = 0; i < length; ++i)
                if (target[i] == '\r' || target[i] == '\n') target[i] = '?';
            target[length] = 0;
        }
    }

    ~RunProfileScope() {
        // resetProfile()/assignment can invalidate an older call chain. Do not
        // restore its stack pointers or charge its elapsed time to new content.
        if (vm.profileNesting.active != this) return;
        vm.profileNesting.active = previous;
        if (previous) return;
        const u32 elapsed = vmProfileClock() - begin;
        vm.profile.totalMicros += elapsed;
        if (!vm.profile.slowAction || elapsed > vm.profile.maxMicros) {
            vm.profile.maxMicros = elapsed;
            vm.profile.slowAction = action;
            const size_t length = std::strlen(target);
            std::memcpy(vm.profile.slowTarget, target, length + 1);
        }
    }
};

bool legacyActionPropFromU32(u32 value, ActionProp* out) {
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

double legacyStrToFloat(const std::string& value) {
    if (value.empty()) {
        return 0.0;
    }
    char* end = 0;
    double result = strtod(value.c_str(), &end);
    return end == value.c_str() ? 0.0 : result;
}

s64 legacyStrToInt(const std::string& value) {
    return (s64)legacyStrToFloat(value);
}

std::string legacyLowerString(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

std::string legacyNumberToString(double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", value);
    return std::string(buf);
}

std::string legacyIntToString(s64 value) {
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

void ActionVM::resetProfile() {
    profile = VmTickProfile();
    profileNesting.active = 0;
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

    RunProfileScope profileScope(*this, index, target);
    u32 pc = index;
    std::vector<std::string> stack;
    std::string currentTarget = target;

    while (true) {
        u32 npc = pc + 1;
        ActionEntry entry;
        if (!reader->getAction(pc, &entry)) {
            break;
        }
        ++profile.instructions;

        switch (entry.action) {
        case ActionEnd:
            return;

        case ActionPush:
            if (entry.payload.hasPayload) {
                stack.push_back(entry.payload.isInteger ? legacyIntToString(entry.payload.integer) : entry.payload.text);
            }
            break;

        case ActionPop:
            popOrDefault(&stack);
            break;

        case ActionSetVariable: {
            std::string val = popOrDefault(&stack);
            std::string var = popOrDefault(&stack);
            vars[legacyLowerString(var)] = val;
            break;
        }

        case ActionGetVariable: {
            std::string name = legacyLowerString(popOrDefault(&stack));
            std::map<std::string, std::string>::const_iterator it = vars.find(name);
            stack.push_back(it == vars.end() ? std::string() : it->second);
            break;
        }

        case ActionAdd: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyNumberToString(legacyStrToFloat(a) + legacyStrToFloat(b)));
            break;
        }

        case ActionSubtract: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyNumberToString(legacyStrToFloat(a) - legacyStrToFloat(b)));
            break;
        }

        case ActionMultiply: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyNumberToString(legacyStrToFloat(a) * legacyStrToFloat(b)));
            break;
        }

        case ActionDivide: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            double bv = legacyStrToFloat(b);
            stack.push_back(legacyNumberToString(bv != 0.0 ? legacyStrToFloat(a) / bv : 0.0));
            break;
        }

        case ActionEquals: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToFloat(a) == legacyStrToFloat(b) ? 1 : 0));
            break;
        }

        case ActionLess: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToFloat(a) < legacyStrToFloat(b) ? 1 : 0));
            break;
        }

        case ActionAnd: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToInt(a) != 0 && legacyStrToInt(b) != 0 ? 1 : 0));
            break;
        }

        case ActionOr: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToInt(a) != 0 || legacyStrToInt(b) != 0 ? 1 : 0));
            break;
        }

        case ActionNot: {
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToInt(a) == 0 ? 1 : 0));
            break;
        }

        case ActionStringEquals: {
            std::string b = popOrDefault(&stack);
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(a == b ? 1 : 0));
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
            stack.push_back(legacyIntToString(a < b ? 1 : 0));
            break;
        }

        case ActionStringLength: {
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString((s64)a.size()));
            break;
        }

        case ActionStringExtract: {
            std::string len = popOrDefault(&stack);
            std::string start = popOrDefault(&stack);
            std::string s = popOrDefault(&stack);
            s64 startIndex = legacyStrToInt(start) - 1;
            if (startIndex < 0) {
                startIndex = 0;
            }
            size_t begin = (size_t)startIndex;
            size_t count = (size_t)std::max<s64>(0, legacyStrToInt(len));
            stack.push_back(begin < s.size() ? s.substr(begin, count) : std::string());
            break;
        }

        case ActionToInteger: {
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(legacyStrToInt(a)));
            break;
        }

        case ActionCharToAscii: {
            std::string a = popOrDefault(&stack);
            stack.push_back(legacyIntToString(a.empty() ? 0 : (unsigned char)a[0]));
            break;
        }

        case ActionAsciiToChar: {
            std::string a = popOrDefault(&stack);
            char ch = (char)(legacyStrToInt(a) & 0xff);
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
            if (legacyStrToInt(cond) != 0 && entry.payload.hasPayload && entry.payload.isInteger) {
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
            u32 frame = (u32)legacyStrToFloat(popOrDefault(&stack));
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
            if (legacyActionPropFromU32((u32)legacyStrToInt(propValue), &prop)) {
                host->setProperty(targetName, prop, value);
            }
            break;
        }

        case ActionGetProperty: {
            std::string propValue = popOrDefault(&stack);
            std::string targetName = popOrDefault(&stack);
            ActionProp prop;
            stack.push_back(legacyActionPropFromU32((u32)legacyStrToInt(propValue), &prop) ? host->getProperty(targetName, prop) : "0");
            break;
        }

        case ActionCloneSprite: {
            std::string depth = popOrDefault(&stack);
            std::string dest = popOrDefault(&stack);
            std::string src = popOrDefault(&stack);
            host->cloneSprite(src, dest, (s32)legacyStrToInt(depth));
            break;
        }

        case ActionRemoveSprite:
            host->removeSprite(popOrDefault(&stack));
            break;

        case ActionCall:
            host->call((u32)legacyStrToInt(popOrDefault(&stack)));
            break;

        case ActionRandomNumber: {
            u32 upper = (u32)legacyStrToInt(popOrDefault(&stack));
            stack.push_back(legacyIntToString(randomBelow(upper)));
            break;
        }

        case ActionGetTime:
            stack.push_back(legacyIntToString(host->getTime()));
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

#endif
