#include "core/input_handler.h"

namespace n32 {

InputHandler::InputHandler()
    : repeatDelay(12), repeatPeriod(3), swapAB(false) {
}

void InputHandler::setRepeatTiming(u32 delay, u32 period) {
    repeatDelay = delay;
    repeatPeriod = period == 0 ? 1 : period;
}

void InputHandler::setSwapAB(bool swap) {
    swapAB = swap;
}

u16 InputHandler::applyABSwap(u16 keycode) {
    if (keycode == KeyA) {
        return KeyB;
    }
    if (keycode == KeyB) {
        return KeyA;
    }
    return keycode;
}

bool InputHandler::isActive(u32 count, u32 delay, u32 period) {
    if (count == 0) {
        return true;
    }
    if (count < delay) {
        return false;
    }
    return ((count - delay) % (period == 0 ? 1 : period)) == 0;
}

void InputHandler::setButtons(const std::vector<u16>& keycodes) {
    std::map<u16, u32> nextCounts;
    std::set<u16> nextActive;

    for (size_t i = 0; i < keycodes.size(); ++i) {
        u16 keycode = swapAB ? applyABSwap(keycodes[i]) : keycodes[i];
        if (nextCounts.find(keycode) != nextCounts.end()) {
            continue;
        }

        std::map<u16, u32>::const_iterator old = heldFrames.find(keycode);
        u32 count = old == heldFrames.end() ? 0 : old->second + 1;
        nextCounts[keycode] = count;
        if (isActive(count, repeatDelay, repeatPeriod)) {
            nextActive.insert(keycode);
        }
    }

    heldFrames.swap(nextCounts);
    activeButtons.swap(nextActive);
}

std::vector<u16> InputHandler::pressedButtons() const {
    std::vector<u16> result;
    for (std::set<u16>::const_iterator it = activeButtons.begin(); it != activeButtons.end(); ++it) {
        result.push_back(*it);
    }
    return result;
}

}
