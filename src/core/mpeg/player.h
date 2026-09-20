#ifndef NATIVE32_MPEG_PLAYER_H
#define NATIVE32_MPEG_PLAYER_H

#include "core/mpeg/video.h"
#include "core/mpeg/playback_budget.h"

namespace n32 {
namespace mpeg {

// Keep common MPEG frames at their decoded size. Larger frames are converted
// directly to a bounded screen-sized buffer, without an intermediate game canvas.
inline void videoOutputSize(size_t w, size_t h, size_t* outW, size_t* outH) {
    *outW = w; *outH = h;
    if (!w || !h) { *outW = *outH = 0; return; }
    if (w <= 512 && h <= 512) return;
    if (w * 272 > h * 480) { *outW = 480; *outH = h * 480 / w; }
    else { *outH = 272; *outW = w * 272 / h; }
    if (!*outW) *outW = 1;
    if (!*outH) *outH = 1;
}

struct AdvanceDiagnostics {
    DecodeDiagnostics pictures;
    unsigned decodeCalls;
    // Successful decode returns, including skipped B slots and a retained
    // reference flushed at EOF. This is not a presented-frame count.
    unsigned slotsAdvanced;
    unsigned decodeMicros;
    unsigned rgbMicros;
    bool outputRequested;
    bool callerReduce;
    bool budgetReduce;
    bool wroteRgb;

    AdvanceDiagnostics()
        : decodeCalls(0), slotsAdvanced(0), decodeMicros(0), rgbMicros(0),
          outputRequested(false), callerReduce(false), budgetReduce(false), wroteRgb(false) {}
};

// Drives an MPEG-1 video stream for real-time playback: decodes frames on a
// time budget and renders the current frame (scaled) into an XRGB8888 buffer.
// Audio is decoded separately by the caller and handed to the audio engine.
class VideoPlayer {
public:
    explicit VideoPlayer(std::vector<u8> videoEs);
    explicit VideoPlayer(Buffer input);

    bool valid() const;
    size_t width() const { return video.width(); }
    size_t height() const { return video.height(); }
    // A retained output buffer may be reused without reconversion until the
    // decoded image changes. Callers must preserve its pixels between calls.
    // Returns true only when this call writes RGB pixels to the destination.
    bool advanceAndRender(double seconds, std::vector<u32>* buffer, size_t width, size_t height,
                          bool reduceWork = false);
    bool isFinished() const;
    double elapsed() const;
    unsigned decodeMicros() const { return lastDecodeMicros; }
    unsigned rgbMicros() const { return lastRgbMicros; }
    unsigned skippedFrames() const { return skippedBFrames; }
    const AdvanceDiagnostics& advanceDiagnostics() const { return lastAdvance; }

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
    AdvanceDiagnostics lastAdvance;
};

}
}

#endif
