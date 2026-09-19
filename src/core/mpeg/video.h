#ifndef NATIVE32_MPEG_VIDEO_H
#define NATIVE32_MPEG_VIDEO_H

#include <psptypes.h>
#include <vector>
#include "core/mpeg/buffer.h"

namespace n32 {
namespace mpeg {

struct Plane {
    size_t width;
    size_t height;
    std::vector<u8> data;

    Plane() : width(0), height(0) {}
    Plane(size_t widthValue, size_t heightValue, const std::vector<u8>& dataValue)
        : width(widthValue), height(heightValue), data(dataValue) {}
};

struct Frame {
    size_t width;
    size_t height;
    Plane y;
    Plane cr;
    Plane cb;

    Frame() : width(0), height(0) {}
    Frame(size_t widthValue, size_t heightValue, const Plane& yValue,
          const Plane& crValue, const Plane& cbValue)
        : width(widthValue), height(heightValue), y(yValue), cr(crValue), cb(cbValue) {}
    void writeRgbScaled(std::vector<u32>* dst, size_t dstW, size_t dstH) const;
};

struct Motion {
    bool fullPx;
    bool isSet;
    int rSize;
    int h;
    int v;

    Motion() : fullPx(false), isSet(false), rSize(0), h(0), v(0) {}
};

// MPEG-1 video decoder. Faithful port of the Java MpegVideo reference.
class Video {
public:
    explicit Video(std::vector<u8> videoEs);
    explicit Video(Buffer input);

    bool hasHeader();
    size_t width() const { return mWidth; }
    size_t height() const { return mHeight; }
    double framerate() const { return mFramerate; }
    double pixelAspectRatio() const { return mPixelAspectRatio; }
    double lastFrameTime() const { return mLastFrameTime; }
    const Frame* frame(size_t index) const { return &frames[index]; }
    size_t frameCount() const { return frames.size(); }

    // Decode the next frame in display order. Returns true and sets
    // *frameIndex when a frame is available, false when the stream is done
    // (in which case the caller should treat the video as finished).
    // With skipB, non-reference B pictures advance time without reconstruction;
    // skipped must be supplied and frameIndex is invalid when *skipped is true.
    bool decode(size_t* frameIndex, bool skipB = false, bool* skipped = 0);

private:
    Buffer buffer;
    void decodeSequenceHeader();
    Frame makeFrame() const;
    void decodePicture();
    void decodeSlice(int slice);
    void decodeMacroblock();
    void decodeMotionVectors();
    int decodeMotionVector(int rSize, int motion);
    void predictMacroblock();
    void copyOrInterpolateMacroblock(int srcIdx, int mh, int mv, bool interpolate);
    void decodeBlock(int block);

    bool hasSequenceHeader;
    int mWidth;
    int mHeight;
    int mbWidth;
    int mbHeight;
    int mbSize;
    int lumaWidth;
    int lumaHeight;
    int chromaWidth;
    int chromaHeight;
    double mFramerate;
    double mPixelAspectRatio;
    double time;
    double mLastFrameTime;
    long framesDecoded;
    int startCode;
    int pictureType;
    Motion motionForward;
    Motion motionBackward;
    int quantizerScale;
    bool sliceBegin;
    int macroblockAddress;
    int mbRow;
    int mbCol;
    int macroblockType;
    bool macroblockIntra;
    int dcPredictor[3];
    int cur;
    int fwd;
    int bwd;
    int blockData[64];
    int intraQuantMatrix[64];
    int nonIntraQuantMatrix[64];
    bool hasReferenceFrame;
    bool assumeNoBFrames;
    std::vector<Frame> frames;
};

}
}

#endif
