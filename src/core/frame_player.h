#ifndef NATIVE32_FRAME_PLAYER_H
#define NATIVE32_FRAME_PLAYER_H

#include <psptypes.h>

namespace n32 {

class FramePlayer {
public:
    FramePlayer();

    void tick();
    bool hasPendingFrame() const;
    u32 takeNextFrame();
    void gotoFrame(u32 frame, bool play);

    u32 currentFrame;
    bool playing;

private:
    bool hasNextFrame;
    u32 nextFrame;
};

}

#endif
