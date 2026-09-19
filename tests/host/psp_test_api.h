#ifndef N32_PSP_TEST_API_H
#define N32_PSP_TEST_API_H
#include "psptypes.h"
#define PSP_AUDIO_SAMPLE_ALIGN(n) (((n) + 63) & ~63)
enum { PSP_AUDIO_FORMAT_STEREO=0, PSP_AUDIO_NEXT_CHANNEL=-1, PSP_AUDIO_VOLUME_MAX=0x8000,
       PSP_THREAD_ATTR_USER=0, PSP_MODULE_AV_AVCODEC=0, PSP_MODULE_AV_MP3=1,
       GU_DIRECT=0, GU_PSM_8888=0, GU_SCISSOR_TEST=0, GU_DEPTH_TEST=0, GU_FALSE=0, GU_TRUE=1,
       PSP_CTRL_MODE_ANALOG=0, PSP_CTRL_UP=1, PSP_CTRL_DOWN=2, PSP_CTRL_LEFT=4,
       PSP_CTRL_RIGHT=8, PSP_CTRL_CROSS=16, PSP_CTRL_CIRCLE=32, PSP_CTRL_TRIANGLE=64,
       PSP_CTRL_HOME=128, PSP_CTRL_SELECT=256, PSP_CTRL_START=512, PSP_CTRL_SQUARE=1024 };
struct SceCtrlData { u32 Buttons; unsigned char Lx,Ly; };
typedef int (*TestThreadEntry)(unsigned int, void*);
int sceKernelCreateSema(const char*, unsigned, int, int, void*);
int sceKernelWaitSema(int, int, void*);
int sceKernelSignalSema(int, int);
int sceKernelDeleteSema(int);
int sceKernelCreateThread(const char*, TestThreadEntry, int, int, unsigned, void*);
int sceKernelStartThread(int, unsigned, void*);
int sceKernelWaitThreadEnd(int, void*);
int sceKernelDeleteThread(int);
int sceKernelDelayThread(unsigned);
u32 sceKernelGetSystemTimeLow();
int sceAudioChReserve(int, int, int);
int sceAudioChRelease(int);
int sceAudioGetChannelRestLen(int);
int sceAudioOutputPannedBlocking(int, int, int, void*);
inline void sceKernelDcacheWritebackRange(const void*, unsigned) {}
inline void sceKernelExitGame() {}
inline int sceUtilityLoadModule(int) { return 0; }
inline int scePowerGetCpuClockFrequencyInt() { return 222; }
inline int scePowerGetBusClockFrequencyInt() { return 111; }
inline int scePowerSetClockFrequency(int, int, int) { return 0; }
inline u32& testButtons() { static u32 buttons = 0; return buttons; }
inline unsigned char& testStickX() { static unsigned char v=128;return v; }
inline unsigned char& testStickY() { static unsigned char v=128;return v; }
inline int sceCtrlReadBufferPositive(SceCtrlData* data, int) { data->Buttons=testButtons(); data->Lx=testStickX(); data->Ly=testStickY(); return 1; }
inline void sceCtrlSetSamplingCycle(int) {}
inline void sceCtrlSetSamplingMode(int) {}
inline void sceDisplayWaitVblankStart() {}
inline void* sceGeEdramGetAddr() { static u32 vram[512*272*2]; return vram; }
inline uintptr_t& testDrawOffset() { static uintptr_t offset = 0; return offset; }
inline void* sceGuSwapBuffers() {
    testDrawOffset() ^= 0x88000u;
    return (void*)testDrawOffset();
}
inline void sceKernelDcacheWritebackInvalidateRange(const void*,unsigned) {}
inline void sceGuTexSync() {}
inline void sceGuCopyImage(int,int sx,int sy,int w,int h,int sw,void* src,int dx,int dy,int dw,void* dst) {
    const u32* from=(const u32*)src;u32* to=(u32*)dst;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)to[(dy+y)*dw+dx+x]=from[(sy+y)*sw+sx+x];
}
inline void sceGuInit() {}
inline void sceGuTerm() {}
inline void sceGuStart(int, void*) {}
inline void sceGuDrawBuffer(int, void*, int) {}
inline void sceGuDispBuffer(int, int, void*, int) {}
inline void sceGuDepthBuffer(void*, int) {}
inline void sceGuOffset(int, int) {}
inline void sceGuViewport(int, int, int, int) {}
inline void sceGuDepthRange(int, int) {}
inline void sceGuScissor(int, int, int, int) {}
inline void sceGuEnable(int) {}
inline void sceGuDisable(int) {}
inline void sceGuFinish() {}
inline void sceGuSync(int, int) {}
inline void sceGuDisplay(int) {}
#endif
