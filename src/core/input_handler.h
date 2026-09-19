#ifndef NATIVE32_INPUT_HANDLER_H
#define NATIVE32_INPUT_HANDLER_H

#include <psptypes.h>
#include <map>
#include <set>
#include <vector>

namespace n32 {

static const u16 KeyLeft = 0x0200;
static const u16 KeyRight = 0x0400;
static const u16 KeyUp = 0x1c00;
static const u16 KeyDown = 0x1e00;
static const u16 KeyA = 0x4000;
static const u16 KeyB = 0x8800;

class InputHandler {
public:
    InputHandler();

    void setRepeatTiming(u32 delay, u32 period);
    void setSwapAB(bool swap);
    void setButtons(const std::vector<u16>& keycodes);
    std::vector<u16> pressedButtons() const;

private:
    static bool isActive(u32 count, u32 delay, u32 period);
    static u16 applyABSwap(u16 keycode);

    std::map<u16, u32> heldFrames;
    std::set<u16> activeButtons;
    u32 repeatDelay;
    u32 repeatPeriod;
    bool swapAB;
};

}

#endif
