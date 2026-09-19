#ifndef N32_TEST_MP3_H
#define N32_TEST_MP3_H
#include <psptypes.h>
typedef long long SceOff;
typedef int SceInt32;
typedef unsigned char SceUChar8;
typedef short SceShort16;
struct SceMp3InitArg {
    SceOff mp3StreamStart,mp3StreamEnd;
    SceUChar8* mp3Buf; SceInt32 mp3BufSize;
    SceUChar8* pcmBuf; SceInt32 pcmBufSize;
};
SceInt32 sceMp3InitResource();
SceInt32 sceMp3TermResource();
SceInt32 sceMp3ReserveMp3Handle(SceMp3InitArg*);
SceInt32 sceMp3ReleaseMp3Handle(SceInt32);
SceInt32 sceMp3Init(SceInt32);
SceInt32 sceMp3GetInfoToAddStreamData(SceInt32,SceUChar8**,SceInt32*,SceInt32*);
SceInt32 sceMp3NotifyAddStreamData(SceInt32,SceInt32);
SceInt32 sceMp3CheckStreamDataNeeded(SceInt32);
SceInt32 sceMp3SetLoopNum(SceInt32,SceInt32);
SceInt32 sceMp3GetSamplingRate(SceInt32);
SceInt32 sceMp3GetMp3ChannelNum(SceInt32);
SceInt32 sceMp3Decode(SceInt32,SceShort16**);
#endif
