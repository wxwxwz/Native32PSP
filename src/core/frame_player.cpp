#include "core/frame_player.h"

namespace n32 {

FramePlayer::FramePlayer()
    : currentFrame(0), playing(true), hasNextFrame(true), nextFrame(1) {
}

void FramePlayer::tick() {
    if (!hasNextFrame && playing) {
        hasNextFrame = true;
        nextFrame = currentFrame + 1;
    }
}

bool FramePlayer::hasPendingFrame() const {
    return hasNextFrame;
}

u32 FramePlayer::takeNextFrame() {
    if (!hasNextFrame) {
        return 0;
    }
    hasNextFrame = false;
    return nextFrame;
}

void FramePlayer::gotoFrame(u32 frame, bool play) {
    hasNextFrame = true;
    nextFrame = frame;
    playing = play;
}

}
