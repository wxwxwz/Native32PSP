#include "core/mpeg/player.h"
#include <algorithm>
#include <utility>
#ifdef PSP
#include <pspkernel.h>
#endif

namespace n32 {
namespace mpeg {

VideoPlayer::VideoPlayer(std::vector<u8> videoEs) : VideoPlayer(Buffer(std::move(videoEs))) {}

VideoPlayer::VideoPlayer(Buffer input)
    : video(std::move(input)), headerOk(false), framerate(25.0), time(0.0), framesShown(0),
      hasCurrentFrame(false), currentFrame(0), finished(false),
      lastDecodeMicros(0), lastRgbMicros(0), skippedBFrames(0), imageDirty(true),
      renderedBuffer(0), renderedPixels(0), renderedWidth(0), renderedHeight(0) {
    headerOk = video.hasHeader();
    if (headerOk) {
        double rate = video.framerate();
        framerate = rate > 0.0 ? rate : 25.0;
    }
}

bool VideoPlayer::valid() const {
    return headerOk;
}

bool VideoPlayer::advanceAndRender(double seconds, std::vector<u32>* buffer, size_t width, size_t height,
                                  bool reduceWork) {
    bool wroteImage = false;
    lastAdvance = AdvanceDiagnostics();
    lastAdvance.outputRequested = buffer != 0;
    lastAdvance.callerReduce = reduceWork;
#ifdef PSP
    unsigned begin = sceKernelGetSystemTimeLow();
    lastAdvance.budgetReduce = workBudget.reduceWork();
    reduceWork = reduceWork || lastAdvance.budgetReduce;
#endif
    if (!finished) {
        time += seconds;
        unsigned long long target = (unsigned long long)(time * framerate);
        while (framesShown <= target) {
            size_t index = 0;
            bool skipped = false;
            ++lastAdvance.decodeCalls;
            if (!video.decode(&index, reduceWork || !buffer || framesShown < target, &skipped,
                              &lastAdvance.pictures)) {
                finished = true;
                break;
            }
            ++lastAdvance.slotsAdvanced;
            if (skipped) {
                ++skippedBFrames;
            } else {
                hasCurrentFrame = true;
                currentFrame = index;
                imageDirty = true;
            }
            ++framesShown;
        }
    }

#ifdef PSP
    unsigned decoded = sceKernelGetSystemTimeLow();
    lastDecodeMicros = decoded - begin;
#endif
    if (hasCurrentFrame && buffer && (imageDirty || renderedBuffer != buffer ||
        renderedPixels != buffer->data() || renderedWidth != width || renderedHeight != height ||
        buffer->size() != width * height)) {
        const Frame* frame = video.frame(currentFrame);
        if (frame) {
            frame->writeRgbScaled(buffer, width, height);
            wroteImage = true;
            imageDirty = false;
            renderedBuffer = buffer;
            renderedPixels = buffer->data();
            renderedWidth = width;
            renderedHeight = height;
        }
    }
#ifdef PSP
    lastRgbMicros = sceKernelGetSystemTimeLow() - decoded;
    workBudget.observe(lastDecodeMicros + lastRgbMicros);
#endif
    lastAdvance.decodeMicros = lastDecodeMicros;
    lastAdvance.rgbMicros = lastRgbMicros;
    lastAdvance.wroteRgb = wroteImage;
    return wroteImage;
}

bool VideoPlayer::isFinished() const {
    return finished;
}

double VideoPlayer::elapsed() const {
    return time;
}

}
}
