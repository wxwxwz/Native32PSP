#ifndef NATIVE32_MPEG_PLAYER_H
#define NATIVE32_MPEG_PLAYER_H

#include "core/mpeg/video.h"
#include "core/mpeg/playback_budget.h"

namespace n32 {
namespace mpeg {

// Drives an MPEG-1 video stream for real-time playback: decodes frames on a
// time budget and renders the current frame (scaled) into an XRGB8888 buffer.
// Audio is decoded separately by the caller and handed to the audio engine.
class VideoPlayer {
public:
    explicit VideoPlayer(std::vector<u8> videoEs);
    explicit VideoPlayer(Buffer input);

    bool valid() const;
    // A retained output buffer may be reused without reconversion until the
    // decoded image changes. Callers must preserve its pixels between calls.
    void advanceAndRender(double seconds, std::vector<u32>* buffer, size_t width, size_t height,
                          bool reduceWork = false);
    bool isFinished() const;
    double elapsed() const;
    unsigned decodeMicros() const { return lastDecodeMicros; }
    unsigned rgbMicros() const { return lastRgbMicros; }
    unsigned skippedFrames() const { return skippedBFrames; }

private:
    Video video;
    bool headerOk;
    double framerate;
    double time;
    unsigned long long framesShown;
    bool hasCurrentFrame;
    size_t currentFrame;
    bool finished;
    unsigned lastDecodeMicros;
    unsigned lastRgbMicros;
    unsigned skippedBFrames;
    bool imageDirty;
    const std::vector<u32>* renderedBuffer;
    const u32* renderedPixels;
    size_t renderedWidth, renderedHeight;
    PlaybackBudget workBudget;
};

}
}

#endif
