#include "core/action_vm.h"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <cstring>
#include <utility>
#if defined(PSP) || defined(N32_TEST_CORE_PROFILE)
#include <pspkernel.h>
#endif

namespace n32 {

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

// Small integers dominate counters, coordinates and variable-name suffixes.
// Nine digits fit exactly in s32 and double. Checking the complete string keeps
// whitespace, suffixes, embedded NULs and every other strtod form on its old path.
static bool parseSmallInteger(const std::string& value, s32* out) {
    if (value.empty()) return false;
    const bool negative = value[0] == '-';
    const size_t first = negative || value[0] == '+' ? 1 : 0;
    const size_t digits = value.size() - first;
    if (digits == 0 || digits > 9) return false;
    u32 magnitude = 0;
    for (size_t i = first; i < value.size(); ++i) {
        const unsigned char ch = (unsigned char)value[i];
        if (ch < '0' || ch > '9') return false;
        magnitude = magnitude * 10 + (ch - '0');
    }
    *out = negative ? -(s32)magnitude : (s32)magnitude;
    return true;
}

static double parseNumberSlow(const std::string& value) {
    char* end = 0;
    double result = strtod(value.c_str(), &end);
    return end == value.c_str() ? 0.0 : result;
}

double strToFloat(const std::string& value) {
    if (value.empty()) return 0.0;
    s32 integer;
    if (parseSmallInteger(value, &integer)) {
        // The integer parser has no signed zero; strtod does.
        return integer == 0 && value[0] == '-' ? -0.0 : (double)integer;
    }
    return parseNumberSlow(value);
}

s64 strToInt(const std::string& value) {
    if (value.empty()) return 0;
    s32 integer;
    // Avoid int -> double -> s64 software conversions on the PSP as well.
    return parseSmallInteger(value, &integer) ? (s64)integer : (s64)parseNumberSlow(value);
}

std::string lowerString(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

static char* writeDecimal32(char* end, u32 value) {
    do {
        *--end = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    return end;
}

static std::string smallIntegerToString(s32 value) {
    char buf[12];
    char* const end = buf + sizeof(buf);
    const bool negative = value < 0;
    // Unsigned subtraction is defined for INT32_MIN too.
    const u32 magnitude = negative ? 0u - (u32)value : (u32)value;
    char* start = writeDecimal32(end, magnitude);
    if (negative) *--start = '-';
    return std::string(start, end);
}

std::string numberToString(double value) {
    // Within nine digits, every integer is exact and %.15g uses decimal fixed
    // notation without rounding. Negative zero must still format as "-0".
    if (value >= -999999999.0 && value <= 999999999.0 &&
        (value != 0.0 || !std::signbit(value))) {
        const s32 integer = (s32)value;
        if ((double)integer == value) return smallIntegerToString(integer);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", value);
    return std::string(buf);
}

std::string intToString(s64 value) {
    if (value >= (-2147483647LL - 1) && value <= 2147483647LL) {
        return smallIntegerToString((s32)value);
    }
    char buf[24];
    char* const end = buf + sizeof(buf);
    char* start = end;
    const bool negative = value < 0;
    // Negating value directly would overflow at INT64_MIN.
    u64 magnitude = negative ? (u64)(-(value + 1)) + 1 : (u64)value;
    // At most two 64-bit divisions; each nine-digit chunk uses 32-bit math.
    // Avoid a software 64-bit divide for every decimal digit on the PSP.
    while (magnitude > 0xffffffffULL) {
        const u64 quotient = magnitude / 1000000000ULL;
        const u32 chunk = (u32)(magnitude - quotient * 1000000000ULL);
        char* const chunkEnd = start;
        start = writeDecimal32(start, chunk);
        while (chunkEnd - start < 9) *--start = '0';
        magnitude = quotient;
    }
    start = writeDecimal32(start, (u32)magnitude);
    if (negative) *--start = '-';
    return std::string(start, end);
}

static std::string popOrDefault(std::vector<std::string>* stack) {
    if (!stack || stack->empty()) {
        return std::string();
    }
    std::string value = std::move(stack->back());
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
        const ActionEntry* cached = reader->getActionRef(pc);
        if (!cached) break;
        // A host callback may grow/reset the reader cache. Consume payloads
        // before calling the host and fetch a fresh entry on the next loop.
        const ActionEntry& entry = *cached;
        ++profile.instructions;

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
            const s64 first = strToInt(start);
            const s64 length = strToInt(len);
            const u64 offset = first > 1 ? (u64)(first - 1) : 0;
            if (offset >= s.size()) {
                stack.push_back(std::string());
                break;
            }
            const size_t begin = (size_t)offset;
            const size_t remaining = s.size() - begin;
            // Scripts can use a negative length while removing an item from a
            // shuffled string. Rust's unsigned length usually selects the tail.
            // Preserve that behavior without its wrapped, sometimes invalid end.
            const size_t count = length < 0 ? remaining :
                (size_t)std::min<u64>((u64)length, (u64)remaining);
            stack.push_back(s.substr(begin, count));
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
