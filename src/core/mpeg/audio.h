#ifndef NATIVE32_MPEG_AUDIO_H
#define NATIVE32_MPEG_AUDIO_H

#include <psptypes.h>
#include <vector>
#include "core/mpeg/buffer.h"

namespace n32 {
namespace mpeg {

struct QuantSpec {
    int levels;
    int group;
    int bits;

    QuantSpec() : levels(0), group(0), bits(0) {}
    QuantSpec(int levelsValue, int groupValue, int bitsValue)
        : levels(levelsValue), group(groupValue), bits(bitsValue) {}
};

struct Samples {
    double time;
    std::vector<float> interleaved;

    Samples() : time(0.0) {}
};

// MPEG-1 Audio Layer II (MP2) elementary stream decoder. Faithful port of the
// Java MpegAudio reference implementation.
class Audio {
public:
    explicit Audio(std::vector<u8> audioEs);
    explicit Audio(Buffer input);

    bool hasHeader();
    u32 sampleRate() const;
    bool decode(Samples* out);

private:
    Buffer buffer;
    int decodeHeader();
    const QuantSpec* readAllocation(int sb, int tab3);
    void readSamples(int ch, int sb, int part);
    void decodeFrame();

    double time;
    long samplesDecoded;
    int sampleRateIndex;
    int bitRateIndex;
    int version;
    int layer;
    int mode;
    int bound;
    int vPos;
    int nextFrameDataSize;
    bool hasHeaderFlag;

    const QuantSpec* allocation[2][32];
    int scaleFactorInfo[2][32];
    int scaleFactor[2][32][3];
    int sample[2][32][3];
    std::vector<float> interleaved;
    float d[1024];
    float v[2][1024];
    float u[32];
};

}
}

#endif
