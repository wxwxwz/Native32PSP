#include "core/mpeg/audio.h"
#include <utility>
#include "core/mpeg/buffer.h"
#include <algorithm>
#include <cmath>

namespace n32 {
namespace mpeg {

static const int SAMPLES_PER_FRAME = 1152;

static const int FRAME_SYNC = 0x7ff;
static const int MPEG_1 = 0x3;
static const int LAYER_II = 0x2;
static const int MODE_JOINT_STEREO = 0x1;
static const int MODE_MONO = 0x3;

static const int SAMPLE_RATE[8] = {
    44100, 48000, 32000, 0,
    22050, 24000, 16000, 0,
};


static const int BIT_RATE[28] = {
    32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,
    8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,
};


static const int SCALEFACTOR_BASE[3] = {
    0x02000000, 0x01965FEA, 0x01428A30,
};


static const int QUANT_TAB_A = 27 | 64;
static const int QUANT_TAB_B = 30 | 64;
static const int QUANT_TAB_C = 8;
static const int QUANT_TAB_D = 12;

static const int QUANT_LUT_STEP_1[2][16] = {
    {0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 0, 0},
};


static const int QUANT_LUT_STEP_2[3][3] = {
    {QUANT_TAB_C, QUANT_TAB_C, QUANT_TAB_D},
    {QUANT_TAB_A, QUANT_TAB_A, QUANT_TAB_A},
    {QUANT_TAB_B, QUANT_TAB_A, QUANT_TAB_B},
};


static const int QUANT_LUT_STEP_3[3][32] = {
    {
        0x44, 0x44, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    {
        0x43, 0x43, 0x43, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0, 0,
    },
    {
        0x45, 0x45, 0x45, 0x45, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x24, 0x24, 0x24, 0x24,
        0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0,
    },
};


static const int QUANT_LUT_STEP_4[6][16] = {
    {0, 1, 2, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 17, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17},
    {0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17},
    {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
};


static const QuantSpec QUANT_TAB[17] = {
    QuantSpec(3, 1, 5), QuantSpec(5, 1, 7), QuantSpec(7, 0, 3), QuantSpec(9, 1, 10),
    QuantSpec(15, 0, 4), QuantSpec(31, 0, 5), QuantSpec(63, 0, 6), QuantSpec(127, 0, 7),
    QuantSpec(255, 0, 8), QuantSpec(511, 0, 9), QuantSpec(1023, 0, 10), QuantSpec(2047, 0, 11),
    QuantSpec(4095, 0, 12), QuantSpec(8191, 0, 13), QuantSpec(16383, 0, 14), QuantSpec(32767, 0, 15),
    QuantSpec(65535, 0, 16),
};


static const float SYNTHESIS_WINDOW[512] = {
    0.0f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -1.0f, -1.0f, -1.0f, -1.0f, -1.5f, -1.5f, -2.0f, -2.0f,
    -2.5f, -2.5f, -3.0f, -3.5f, -3.5f, -4.0f, -4.5f, -5.0f, -5.5f, -6.5f, -7.0f, -8.0f, -8.5f, -9.5f, -10.5f,
    -12.0f, -13.0f, -14.5f, -15.5f, -17.5f, -19.0f, -20.5f, -22.5f, -24.5f, -26.5f, -29.0f, -31.5f, -34.0f,
    -36.5f, -39.5f, -42.5f, -45.5f, -48.5f, -52.0f, -55.5f, -58.5f, -62.5f, -66.0f, -69.5f, -73.5f, -77.0f,
    -80.5f, -84.5f, -88.0f, -91.5f, -95.0f, -98.0f, -101.0f, -104.0f, 106.5f, 109.0f, 111.0f, 112.5f, 113.5f,
    114.0f, 114.0f, 113.5f, 112.0f, 110.5f, 107.5f, 104.0f, 100.0f, 94.5f, 88.5f, 81.5f, 73.0f, 63.5f, 53.0f,
    41.5f, 28.5f, 14.5f, -1.0f, -18.0f, -36.0f, -55.5f, -76.5f, -98.5f, -122.0f, -147.0f, -173.5f, -200.5f,
    -229.5f, -259.5f, -290.5f, -322.5f, -355.5f, -389.5f, -424.0f, -459.5f, -495.5f, -532.0f, -568.5f, -605.0f,
    -641.5f, -678.0f, -714.0f, -749.0f, -783.5f, -817.0f, -849.0f, -879.5f, -908.5f, -935.0f, -959.5f, -981.0f,
    -1000.5f, -1016.0f, -1028.5f, -1037.5f, -1042.5f, -1043.5f, -1040.0f, -1031.5f, 1018.5f, 1000.0f, 976.0f,
    946.5f, 911.0f, 869.5f, 822.0f, 767.5f, 707.0f, 640.0f, 565.5f, 485.0f, 397.0f, 302.5f, 201.0f, 92.5f,
    -22.5f, -144.0f, -272.5f, -407.0f, -547.5f, -694.0f, -846.0f, -1003.0f, -1165.0f, -1331.5f, -1502.0f,
    -1675.5f, -1852.5f, -2031.5f, -2212.5f, -2394.0f, -2576.5f, -2758.5f, -2939.5f, -3118.5f, -3294.5f, -3467.5f,
    -3635.5f, -3798.5f, -3955.0f, -4104.5f, -4245.5f, -4377.5f, -4499.0f, -4609.5f, -4708.0f, -4792.5f, -4863.5f,
    -4919.0f, -4958.0f, -4979.5f, -4983.0f, -4967.5f, -4931.5f, -4875.0f, -4796.0f, -4694.5f, -4569.5f, -4420.0f,
    -4246.0f, -4046.0f, -3820.0f, -3567.0f, 3287.0f, 2979.5f, 2644.0f, 2280.5f, 1888.0f, 1467.5f, 1018.5f,
    541.0f, 35.0f, -499.0f, -1061.0f, -1650.0f, -2266.5f, -2909.0f, -3577.0f, -4270.0f, -4987.5f, -5727.5f,
    -6490.0f, -7274.0f, -8077.5f, -8899.5f, -9739.0f, -10594.5f, -11464.5f, -12347.0f, -13241.0f, -14144.5f,
    -15056.0f, -15973.5f, -16895.5f, -17820.0f, -18744.5f, -19668.0f, -20588.0f, -21503.0f, -22410.5f, -23308.5f,
    -24195.0f, -25068.5f, -25926.5f, -26767.0f, -27589.0f, -28389.0f, -29166.5f, -29919.0f, -30644.5f, -31342.0f,
    -32009.5f, -32645.0f, -33247.0f, -33814.5f, -34346.0f, -34839.5f, -35295.0f, -35710.0f, -36084.5f, -36417.5f,
    -36707.5f, -36954.0f, -37156.5f, -37315.0f, -37428.0f, -37496.0f, 37519.0f, 37496.0f, 37428.0f, 37315.0f,
    37156.5f, 36954.0f, 36707.5f, 36417.5f, 36084.5f, 35710.0f, 35295.0f, 34839.5f, 34346.0f, 33814.5f, 33247.0f,
    32645.0f, 32009.5f, 31342.0f, 30644.5f, 29919.0f, 29166.5f, 28389.0f, 27589.0f, 26767.0f, 25926.5f, 25068.5f,
    24195.0f, 23308.5f, 22410.5f, 21503.0f, 20588.0f, 19668.0f, 18744.5f, 17820.0f, 16895.5f, 15973.5f, 15056.0f,
    14144.5f, 13241.0f, 12347.0f, 11464.5f, 10594.5f, 9739.0f, 8899.5f, 8077.5f, 7274.0f, 6490.0f, 5727.5f,
    4987.5f, 4270.0f, 3577.0f, 2909.0f, 2266.5f, 1650.0f, 1061.0f, 499.0f, -35.0f, -541.0f, -1018.5f, -1467.5f,
    -1888.0f, -2280.5f, -2644.0f, -2979.5f, 3287.0f, 3567.0f, 3820.0f, 4046.0f, 4246.0f, 4420.0f, 4569.5f,
    4694.5f, 4796.0f, 4875.0f, 4931.5f, 4967.5f, 4983.0f, 4979.5f, 4958.0f, 4919.0f, 4863.5f, 4792.5f, 4708.0f,
    4609.5f, 4499.0f, 4377.5f, 4245.5f, 4104.5f, 3955.0f, 3798.5f, 3635.5f, 3467.5f, 3294.5f, 3118.5f, 2939.5f,
    2758.5f, 2576.5f, 2394.0f, 2212.5f, 2031.5f, 1852.5f, 1675.5f, 1502.0f, 1331.5f, 1165.0f, 1003.0f, 846.0f,
    694.0f, 547.5f, 407.0f, 272.5f, 144.0f, 22.5f, -92.5f, -201.0f, -302.5f, -397.0f, -485.0f, -565.5f, -640.0f,
    -707.0f, -767.5f, -822.0f, -869.5f, -911.0f, -946.5f, -976.0f, -1000.0f, 1018.5f, 1031.5f, 1040.0f, 1043.5f,
    1042.5f, 1037.5f, 1028.5f, 1016.0f, 1000.5f, 981.0f, 959.5f, 935.0f, 908.5f, 879.5f, 849.0f, 817.0f, 783.5f,
    749.0f, 714.0f, 678.0f, 641.5f, 605.0f, 568.5f, 532.0f, 495.5f, 459.5f, 424.0f, 389.5f, 355.5f, 322.5f,
    290.5f, 259.5f, 229.5f, 200.5f, 173.5f, 147.0f, 122.0f, 98.5f, 76.5f, 55.5f, 36.0f, 18.0f, 1.0f, -14.5f,
    -28.5f, -41.5f, -53.0f, -63.5f, -73.0f, -81.5f, -88.5f, -94.5f, -100.0f, -104.0f, -107.5f, -110.5f, -112.0f,
    -113.5f, -114.0f, -114.0f, -113.5f, -112.5f, -111.0f, -109.0f, 106.5f, 104.0f, 101.0f, 98.0f, 95.0f, 91.5f,
    88.0f, 84.5f, 80.5f, 77.0f, 73.5f, 69.5f, 66.0f, 62.5f, 58.5f, 55.5f, 52.0f, 48.5f, 45.5f, 42.5f, 39.5f,
    36.5f, 34.0f, 31.5f, 29.0f, 26.5f, 24.5f, 22.5f, 20.5f, 19.0f, 17.5f, 15.5f, 14.5f, 13.0f, 12.0f, 10.5f,
    9.5f, 8.5f, 8.0f, 7.0f, 6.5f, 5.5f, 5.0f, 4.5f, 4.0f, 3.5f, 3.5f, 3.0f, 2.5f, 2.5f, 2.0f, 2.0f, 1.5f, 1.5f,
    1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
};



// Defined below, used by decodeFrame().
static float g(int s[32][3], int i, int ss);
static void idct36(int s[32][3], int ss, float d[1024], int dp);

Audio::Audio(std::vector<u8> audioEs) : Audio(Buffer(std::move(audioEs))) {}

Audio::Audio(Buffer input)
    : buffer(std::move(input)), time(0.0), samplesDecoded(0), sampleRateIndex(3),
      bitRateIndex(0), version(0), layer(0), mode(0), bound(0), vPos(0), nextFrameDataSize(0),
      hasHeaderFlag(false), interleaved(SAMPLES_PER_FRAME * 2, 0.0f) {
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 32; ++j) {
            allocation[i][j] = 0;
        }
    }
    for (int i = 0; i < 512; ++i) {
        d[i] = SYNTHESIS_WINDOW[i];
        d[i + 512] = SYNTHESIS_WINDOW[i];
    }
    // The synthesis loop reads the whole of v[] while idct36 only writes a
    // 32-entry window per call, so it must start zeroed.
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 1024; ++i) {
            v[ch][i] = 0.0f;
        }
    }
    for (int i = 0; i < 32; ++i) {
        u[i] = 0.0f;
    }
    for (int ch = 0; ch < 2; ++ch) {
        for (int sb = 0; sb < 32; ++sb) {
            scaleFactorInfo[ch][sb] = 0;
            for (int p = 0; p < 3; ++p) {
                scaleFactor[ch][sb][p] = 0;
                sample[ch][sb][p] = 0;
            }
        }
    }
    nextFrameDataSize = decodeHeader();
}

bool Audio::hasHeader() {
    if (hasHeaderFlag) {
        return true;
    }
    nextFrameDataSize = decodeHeader();
    return hasHeaderFlag;
}

u32 Audio::sampleRate() const {
    return hasHeaderFlag ? (u32)SAMPLE_RATE[sampleRateIndex] : 0;
}

bool Audio::decode(Samples* out) {
    buffer.discardReadBytes();
    if (nextFrameDataSize == 0) {
        if (!buffer.has(48)) {
            return false;
        }
        nextFrameDataSize = decodeHeader();
    }
    if (nextFrameDataSize == 0 || !buffer.has((size_t)nextFrameDataSize << 3)) {
        return false;
    }

    interleaved.resize(SAMPLES_PER_FRAME * 2);
    decodeFrame();
    nextFrameDataSize = 0;

    double sampleTime = time;
    samplesDecoded += SAMPLES_PER_FRAME;
    time = (double)samplesDecoded / (double)SAMPLE_RATE[sampleRateIndex];
    if (out) {
        out->time = sampleTime;
        out->interleaved.swap(interleaved);
    }
    return true;
}

int Audio::decodeHeader() {
    if (!buffer.has(48)) {
        return 0;
    }
    buffer.skipBytes(0x00);
    int sync = (int)buffer.read(11);

    if (sync != FRAME_SYNC && !buffer.findFrameSync()) {
        return 0;
    }

    version = (int)buffer.read(2);
    layer = (int)buffer.read(2);
    bool hasCrc = buffer.read(1) == 0;

    if (version != MPEG_1 || layer != LAYER_II) {
        return 0;
    }

    int parsedBitRateIndex = (int)buffer.read(4) - 1;
    if (parsedBitRateIndex < 0 || parsedBitRateIndex > 13) {
        return 0;
    }

    int parsedSampleRateIndex = (int)buffer.read(2);
    if (parsedSampleRateIndex == 3) {
        return 0;
    }

    int padding = (int)buffer.read(1);
    buffer.skip(1);
    int parsedMode = (int)buffer.read(2);

    if (hasHeaderFlag &&
        (bitRateIndex != parsedBitRateIndex ||
         sampleRateIndex != parsedSampleRateIndex ||
         mode != parsedMode)) {
        return 0;
    }

    bitRateIndex = parsedBitRateIndex;
    sampleRateIndex = parsedSampleRateIndex;
    mode = parsedMode;
    hasHeaderFlag = true;

    if (mode == MODE_JOINT_STEREO) {
        bound = ((int)buffer.read(2) + 1) << 2;
    } else {
        buffer.skip(2);
        bound = mode == MODE_MONO ? 0 : 32;
    }

    buffer.skip(4);
    if (hasCrc) {
        buffer.skip(16);
    }

    int bitrate = BIT_RATE[bitRateIndex];
    int sampleRate = SAMPLE_RATE[sampleRateIndex];
    int frameSize = (144000 * bitrate / sampleRate) + padding;
    return frameSize - (hasCrc ? 6 : 4);
}

const QuantSpec* Audio::readAllocation(int sb, int tab3) {
    int tab4 = QUANT_LUT_STEP_3[tab3][sb];
    int nbal = tab4 >> 4;
    int row = tab4 & 15;
    int qtab = QUANT_LUT_STEP_4[row][(int)buffer.read(nbal)];
    return qtab != 0 ? &QUANT_TAB[qtab - 1] : 0;
}

void Audio::readSamples(int ch, int sb, int part) {
    const QuantSpec* q = allocation[ch][sb];
    if (!q) {
        sample[ch][sb][0] = 0;
        sample[ch][sb][1] = 0;
        sample[ch][sb][2] = 0;
        return;
    }

    int sf = scaleFactor[ch][sb][part];
    if (sf == 63) {
        sf = 0;
    } else {
        int shift = sf / 3;
        sf = (SCALEFACTOR_BASE[sf % 3] + ((1 << shift) >> 1)) >> shift;
    }

    int adj = q->levels;
    if (q->group != 0) {
        int val = (int)buffer.read(q->bits);
        sample[ch][sb][0] = val % adj;
        val /= adj;
        sample[ch][sb][1] = val % adj;
        sample[ch][sb][2] = val / adj;
    } else {
        for (int i = 0; i < 3; ++i) {
            sample[ch][sb][i] = (int)buffer.read(q->bits);
        }
    }

    int scale = 65536 / (adj + 1);
    adj = ((adj + 1) >> 1) - 1;
    for (int i = 0; i < 3; ++i) {
        int val = (adj - sample[ch][sb][i]) * scale;
        sample[ch][sb][i] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;
    }
}

void Audio::decodeFrame() {
    int tab1 = mode == MODE_MONO ? 0 : 1;
    int tab2 = QUANT_LUT_STEP_1[tab1][bitRateIndex];
    int tab3Value = QUANT_LUT_STEP_2[tab2][sampleRateIndex];
    int sblimit = tab3Value & 63;
    int tab3 = tab3Value >> 6;

    if (bound > sblimit) {
        bound = sblimit;
    }
    int localBound = bound;

    for (int sb = 0; sb < localBound; ++sb) {
        allocation[0][sb] = readAllocation(sb, tab3);
        allocation[1][sb] = readAllocation(sb, tab3);
    }
    for (int sb = localBound; sb < sblimit; ++sb) {
        const QuantSpec* a = readAllocation(sb, tab3);
        allocation[0][sb] = a;
        allocation[1][sb] = a;
    }

    int channels = mode == MODE_MONO ? 1 : 2;
    for (int sb = 0; sb < sblimit; ++sb) {
        for (int ch = 0; ch < channels; ++ch) {
            if (allocation[ch][sb]) {
                scaleFactorInfo[ch][sb] = (int)buffer.read(2);
            }
        }
        if (mode == MODE_MONO) {
            scaleFactorInfo[1][sb] = scaleFactorInfo[0][sb];
        }
    }

    for (int sb = 0; sb < sblimit; ++sb) {
        for (int ch = 0; ch < channels; ++ch) {
            if (allocation[ch][sb]) {
                int info = scaleFactorInfo[ch][sb];
                int* sf = scaleFactor[ch][sb];
                if (info == 0) {
                    sf[0] = (int)buffer.read(6);
                    sf[1] = (int)buffer.read(6);
                    sf[2] = (int)buffer.read(6);
                } else if (info == 1) {
                    int a = (int)buffer.read(6);
                    sf[0] = a;
                    sf[1] = a;
                    sf[2] = (int)buffer.read(6);
                } else if (info == 2) {
                    int a = (int)buffer.read(6);
                    sf[0] = a;
                    sf[1] = a;
                    sf[2] = a;
                } else {
                    sf[0] = (int)buffer.read(6);
                    int a = (int)buffer.read(6);
                    sf[1] = a;
                    sf[2] = a;
                }
            }
        }
        if (mode == MODE_MONO) {
            scaleFactor[1][sb][0] = scaleFactor[0][sb][0];
            scaleFactor[1][sb][1] = scaleFactor[0][sb][1];
            scaleFactor[1][sb][2] = scaleFactor[0][sb][2];
        }
    }

    // The fixed 3*4*3 synthesis blocks below each write 32 stereo frames,
    // covering all 2304 output floats even for mono or zero allocations.
    int outPos = 0;
    for (int part = 0; part < 3; ++part) {
        for (int granule = 0; granule < 4; ++granule) {
            for (int sb = 0; sb < localBound; ++sb) {
                readSamples(0, sb, part);
                readSamples(1, sb, part);
            }
            for (int sb = localBound; sb < sblimit; ++sb) {
                readSamples(0, sb, part);
                sample[1][sb][0] = sample[0][sb][0];
                sample[1][sb][1] = sample[0][sb][1];
                sample[1][sb][2] = sample[0][sb][2];
            }
            for (int sb = sblimit; sb < 32; ++sb) {
                sample[0][sb][0] = 0; sample[0][sb][1] = 0; sample[0][sb][2] = 0;
                sample[1][sb][0] = 0; sample[1][sb][1] = 0; sample[1][sb][2] = 0;
            }

            for (int p = 0; p < 3; ++p) {
                vPos = (vPos - 64) & 1023;
                for (int ch = 0; ch < 2; ++ch) {
                    idct36(sample[ch], p, v[ch], vPos);
                    std::fill(u, u + 32, 0.0f);
                    int dIndex = 512 - (vPos >> 1);
                    int vIndex = (vPos % 128) >> 1;
                    while (vIndex < 1024) {
                        for (int i = 0; i < 32; ++i) {
                            u[i] += d[dIndex] * v[ch][vIndex];
                            ++dIndex;
                            ++vIndex;
                        }
                        vIndex += 128 - 32;
                        dIndex += 64 - 32;
                    }

                    dIndex -= 512 - 32;
                    vIndex = (128 - 32 + 1024) - vIndex;
                    while (vIndex < 1024) {
                        for (int i = 0; i < 32; ++i) {
                            u[i] += d[dIndex] * v[ch][vIndex];
                            ++dIndex;
                            ++vIndex;
                        }
                        vIndex += 128 - 32;
                        dIndex += 64 - 32;
                    }

                    for (int j = 0; j < 32; ++j) {
                        interleaved[(size_t)((outPos + j) << 1) + ch] = u[j] / -1090519040.0f;
                    }
                }
                outPos += 32;
            }
        }
    }

    buffer.align();
}

// Keep the synthesis transform in single precision: PSP doubles otherwise
// call software helpers inside every MP2 subband synthesis step.
static float g(int s[32][3], int i, int ss) {
    return s[i][ss];
}

static void idct36(int s[32][3], int ss, float d[1024], int dp) {
    float t01 = g(s, 0, ss) + g(s, 31, ss);
    float t02 = (g(s, 0, ss) - g(s, 31, ss)) * 0.500602998235f;
    float t03 = g(s, 1, ss) + g(s, 30, ss);
    float t04 = (g(s, 1, ss) - g(s, 30, ss)) * 0.505470959898f;
    float t05 = g(s, 2, ss) + g(s, 29, ss);
    float t06 = (g(s, 2, ss) - g(s, 29, ss)) * 0.515447309923f;
    float t07 = g(s, 3, ss) + g(s, 28, ss);
    float t08 = (g(s, 3, ss) - g(s, 28, ss)) * 0.53104259109f;
    float t09 = g(s, 4, ss) + g(s, 27, ss);
    float t10 = (g(s, 4, ss) - g(s, 27, ss)) * 0.553103896034f;
    float t11 = g(s, 5, ss) + g(s, 26, ss);
    float t12 = (g(s, 5, ss) - g(s, 26, ss)) * 0.582934968206f;
    float t13 = g(s, 6, ss) + g(s, 25, ss);
    float t14 = (g(s, 6, ss) - g(s, 25, ss)) * 0.622504123036f;
    float t15 = g(s, 7, ss) + g(s, 24, ss);
    float t16 = (g(s, 7, ss) - g(s, 24, ss)) * 0.674808341455f;
    float t17 = g(s, 8, ss) + g(s, 23, ss);
    float t18 = (g(s, 8, ss) - g(s, 23, ss)) * 0.744536271002f;
    float t19 = g(s, 9, ss) + g(s, 22, ss);
    float t20 = (g(s, 9, ss) - g(s, 22, ss)) * 0.839349645416f;
    float t21 = g(s, 10, ss) + g(s, 21, ss);
    float t22 = (g(s, 10, ss) - g(s, 21, ss)) * 0.972568237862f;
    float t23 = g(s, 11, ss) + g(s, 20, ss);
    float t24 = (g(s, 11, ss) - g(s, 20, ss)) * 1.16943993343f;
    float t25 = g(s, 12, ss) + g(s, 19, ss);
    float t26 = (g(s, 12, ss) - g(s, 19, ss)) * 1.48416461631f;
    float t27 = g(s, 13, ss) + g(s, 18, ss);
    float t28 = (g(s, 13, ss) - g(s, 18, ss)) * 2.05778100995f;
    float t29 = g(s, 14, ss) + g(s, 17, ss);
    float t30 = (g(s, 14, ss) - g(s, 17, ss)) * 3.40760841847f;
    float t31 = g(s, 15, ss) + g(s, 16, ss);
    float t32 = (g(s, 15, ss) - g(s, 16, ss)) * 10.1900081235f;

    float t33;
    t33 = t01 + t31;
    t31 = (t01 - t31) * 0.502419286188f;
    t01 = t03 + t29;
    t29 = (t03 - t29) * 0.52249861494f;
    t03 = t05 + t27;
    t27 = (t05 - t27) * 0.566944034816f;
    t05 = t07 + t25;
    t25 = (t07 - t25) * 0.64682178336f;
    t07 = t09 + t23;
    t23 = (t09 - t23) * 0.788154623451f;
    t09 = t11 + t21;
    t21 = (t11 - t21) * 1.06067768599f;
    t11 = t13 + t19;
    t19 = (t13 - t19) * 1.72244709824f;
    t13 = t15 + t17;
    t17 = (t15 - t17) * 5.10114861869f;

    t15 = t33 + t13;
    t13 = (t33 - t13) * 0.509795579104f;
    t33 = t01 + t11;
    t01 = (t01 - t11) * 0.601344886935f;
    t11 = t03 + t09;
    t09 = (t03 - t09) * 0.899976223136f;
    t03 = t05 + t07;
    t07 = (t05 - t07) * 2.56291544774f;
    t05 = t15 + t03;
    t15 = (t15 - t03) * 0.541196100146f;
    t03 = t33 + t11;
    t11 = (t33 - t11) * 1.30656296488f;
    t33 = t05 + t03;
    t05 = (t05 - t03) * 0.707106781187f;
    t03 = t15 + t11;
    t15 = (t15 - t11) * 0.707106781187f;
    t03 += t15;
    t11 = t13 + t07;
    t13 = (t13 - t07) * 0.541196100146f;
    t07 = t01 + t09;
    t09 = (t01 - t09) * 1.30656296488f;
    t01 = t11 + t07;
    t07 = (t11 - t07) * 0.707106781187f;
    t11 = t13 + t09;
    t13 = (t13 - t09) * 0.707106781187f;
    t11 += t13;
    t01 += t11;
    t11 += t07;
    t07 += t13;
    t09 = t31 + t17;
    t31 = (t31 - t17) * 0.509795579104f;
    t17 = t29 + t19;
    t29 = (t29 - t19) * 0.601344886935f;
    t19 = t27 + t21;
    t21 = (t27 - t21) * 0.899976223136f;
    t27 = t25 + t23;
    t23 = (t25 - t23) * 2.56291544774f;
    t25 = t09 + t27;
    t09 = (t09 - t27) * 0.541196100146f;
    t27 = t17 + t19;
    t19 = (t17 - t19) * 1.30656296488f;
    t17 = t25 + t27;
    t27 = (t25 - t27) * 0.707106781187f;
    t25 = t09 + t19;
    t19 = (t09 - t19) * 0.707106781187f;
    t25 += t19;
    t09 = t31 + t23;
    t31 = (t31 - t23) * 0.541196100146f;
    t23 = t29 + t21;
    t21 = (t29 - t21) * 1.30656296488f;
    t29 = t09 + t23;
    t23 = (t09 - t23) * 0.707106781187f;
    t09 = t31 + t21;
    t31 = (t31 - t21) * 0.707106781187f;
    t09 += t31;
    t29 += t09;
    t09 += t23;
    t23 += t31;
    t17 += t29;
    t29 += t25;
    t25 += t09;
    t09 += t27;
    t27 += t23;
    t23 += t19;
    t19 += t31;

    t21 = t02 + t32;
    t02 = (t02 - t32) * 0.500602998235f;
    t32 = t04 + t30;
    t04 = (t04 - t30) * 0.52249861494f;
    t30 = t06 + t28;
    t28 = (t06 - t28) * 0.566944034816f;
    t06 = t08 + t26;
    t08 = (t08 - t26) * 0.64682178336f;
    t26 = t10 + t24;
    t10 = (t10 - t24) * 0.788154623451f;
    t24 = t12 + t22;
    t22 = (t12 - t22) * 1.06067768599f;
    t12 = t14 + t20;
    t20 = (t14 - t20) * 1.72244709824f;
    t14 = t16 + t18;
    t16 = (t16 - t18) * 5.10114861869f;

    t18 = t21 + t14;
    t14 = (t21 - t14) * 0.509795579104f;
    t21 = t32 + t12;
    t32 = (t32 - t12) * 0.601344886935f;
    t12 = t30 + t24;
    t24 = (t30 - t24) * 0.899976223136f;
    t30 = t06 + t26;
    t26 = (t06 - t26) * 2.56291544774f;
    t06 = t18 + t30;
    t18 = (t18 - t30) * 0.541196100146f;
    t30 = t21 + t12;
    t12 = (t21 - t12) * 1.30656296488f;
    t21 = t06 + t30;
    t30 = (t06 - t30) * 0.707106781187f;
    t06 = t18 + t12;
    t12 = (t18 - t12) * 0.707106781187f;
    t06 += t12;
    t18 = t14 + t26;
    t26 = (t14 - t26) * 0.541196100146f;
    t14 = t32 + t24;
    t24 = (t32 - t24) * 1.30656296488f;
    t32 = t18 + t14;
    t14 = (t18 - t14) * 0.707106781187f;
    t18 = t26 + t24;
    t24 = (t26 - t24) * 0.707106781187f;
    t18 += t24;
    t32 += t18;
    t18 += t14;
    t26 = t14 + t24;
    t14 = t02 + t16;
    t02 = (t02 - t16) * 0.509795579104f;
    t16 = t04 + t20;
    t04 = (t04 - t20) * 0.601344886935f;
    t20 = t28 + t22;
    t22 = (t28 - t22) * 0.899976223136f;
    t28 = t08 + t10;
    t10 = (t08 - t10) * 2.56291544774f;
    t08 = t14 + t28;
    t14 = (t14 - t28) * 0.541196100146f;
    t28 = t16 + t20;
    t20 = (t16 - t20) * 1.30656296488f;
    t16 = t08 + t28;
    t28 = (t08 - t28) * 0.707106781187f;
    t08 = t14 + t20;
    t20 = (t14 - t20) * 0.707106781187f;
    t08 += t20;
    t14 = t02 + t10;
    t02 = (t02 - t10) * 0.541196100146f;
    t10 = t04 + t22;
    t22 = (t04 - t22) * 1.30656296488f;
    t04 = t14 + t10;
    t10 = (t14 - t10) * 0.707106781187f;
    t14 = t02 + t22;
    t02 = (t02 - t22) * 0.707106781187f;
    t14 += t02;
    t04 += t14;
    t14 += t10;
    t10 += t02;
    t16 += t04;
    t04 += t08;
    t08 += t14;
    t14 += t28;
    t28 += t10;
    t10 += t20;
    t20 += t02;
    t21 += t16;
    t16 += t32;
    t32 += t04;
    t04 += t06;
    t06 += t08;
    t08 += t18;
    t18 += t14;
    t14 += t30;
    t30 += t28;
    t28 += t26;
    t26 += t10;
    t10 += t12;
    t12 += t20;
    t20 += t24;
    t24 += t02;

    d[dp + 48] = (float)(-t33);
    d[dp + 49] = (float)(-t21);
    d[dp + 47] = (float)(-t21);
    d[dp + 50] = (float)(-t17);
    d[dp + 46] = (float)(-t17);
    d[dp + 51] = (float)(-t16);
    d[dp + 45] = (float)(-t16);
    d[dp + 52] = (float)(-t01);
    d[dp + 44] = (float)(-t01);
    d[dp + 53] = (float)(-t32);
    d[dp + 43] = (float)(-t32);
    d[dp + 54] = (float)(-t29);
    d[dp + 42] = (float)(-t29);
    d[dp + 55] = (float)(-t04);
    d[dp + 41] = (float)(-t04);
    d[dp + 56] = (float)(-t03);
    d[dp + 40] = (float)(-t03);
    d[dp + 57] = (float)(-t06);
    d[dp + 39] = (float)(-t06);
    d[dp + 58] = (float)(-t25);
    d[dp + 38] = (float)(-t25);
    d[dp + 59] = (float)(-t08);
    d[dp + 37] = (float)(-t08);
    d[dp + 60] = (float)(-t11);
    d[dp + 36] = (float)(-t11);
    d[dp + 61] = (float)(-t18);
    d[dp + 35] = (float)(-t18);
    d[dp + 62] = (float)(-t09);
    d[dp + 34] = (float)(-t09);
    d[dp + 63] = (float)(-t14);
    d[dp + 33] = (float)(-t14);
    d[dp + 32] = (float)(-t05);
    d[dp] = (float)(t05);
    d[dp + 31] = (float)(-t30);
    d[dp + 1] = (float)(t30);
    d[dp + 30] = (float)(-t27);
    d[dp + 2] = (float)(t27);
    d[dp + 29] = (float)(-t28);
    d[dp + 3] = (float)(t28);
    d[dp + 28] = (float)(-t07);
    d[dp + 4] = (float)(t07);
    d[dp + 27] = (float)(-t26);
    d[dp + 5] = (float)(t26);
    d[dp + 26] = (float)(-t23);
    d[dp + 6] = (float)(t23);
    d[dp + 25] = (float)(-t10);
    d[dp + 7] = (float)(t10);
    d[dp + 24] = (float)(-t15);
    d[dp + 8] = (float)(t15);
    d[dp + 23] = (float)(-t12);
    d[dp + 9] = (float)(t12);
    d[dp + 22] = (float)(-t19);
    d[dp + 10] = (float)(t19);
    d[dp + 21] = (float)(-t20);
    d[dp + 11] = (float)(t20);
    d[dp + 20] = (float)(-t13);
    d[dp + 12] = (float)(t13);
    d[dp + 19] = (float)(-t24);
    d[dp + 13] = (float)(t24);
    d[dp + 18] = (float)(-t31);
    d[dp + 14] = (float)(t31);
    d[dp + 17] = (float)(-t02);
    d[dp + 15] = (float)(t02);
    d[dp + 16] = (float)(0.0f);
}

}
}
