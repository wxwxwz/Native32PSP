#ifndef NATIVE32_ACTION_VM_H
#define NATIVE32_ACTION_VM_H

#include "core/native32_reader.h"
#include <map>

namespace n32 {

enum ActionProp {
    ActionPropX = 0,
    ActionPropY = 1,
    ActionPropXScale = 2,
    ActionPropYScale = 3,
    ActionPropCurrentFrame = 4,
    ActionPropTotalFrames = 5,
    ActionPropAlpha = 6,
    ActionPropVisible = 7,
    ActionPropWidth = 8,
    ActionPropHeight = 9,
    ActionPropName = 13
};

bool actionPropFromU32(u32 value, ActionProp* out);

class VmHost {
public:
    virtual ~VmHost() {}

    virtual void stop(const std::string& target) = 0;
    virtual void play(const std::string& target) = 0;
    virtual u32 getFrame(const std::string& target) = 0;
    virtual void gotoFrame(const std::string& target, u32 frame, bool playing) = 0;
    virtual void stopSounds(const std::string& target) = 0;
    virtual void setProperty(const std::string& target, ActionProp prop, const std::string& value) = 0;
    virtual std::string getProperty(const std::string& target, ActionProp prop) = 0;
    virtual void cloneSprite(const std::string& src, const std::string& dest, s32 depth) = 0;
    virtual void removeSprite(const std::string& name) = 0;
    virtual void call(u32 frame) = 0;
    virtual u32 getTime() const = 0;
    virtual void getUrl(const std::string& url, const std::string& target) = 0;
    virtual void runFrameActions(u32 frame) = 0;
};

// Per-tick diagnostics: recursive calls contribute instructions/calls/depth,
// while wall time is charged only to their outermost run. Targets own no heap.
struct VmTickProfile {
    u32 calls, instructions, maxDepth, maxMicros, slowAction;
    u64 totalMicros;
    char slowTarget[48];
    VmTickProfile()
        : calls(0), instructions(0), maxDepth(0), maxMicros(0), slowAction(0),
          totalMicros(0), slowTarget() {}
};

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

double strToFloat(const std::string& value);
s64 strToInt(const std::string& value);
std::string lowerString(const std::string& value);
std::string numberToString(double value);
std::string intToString(s64 value);

}

#endif
