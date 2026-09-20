#ifndef N32_PSP_TEST_API_H
#define N32_PSP_TEST_API_H
#include "psptypes.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
#define PSP_AUDIO_SAMPLE_ALIGN(n) (((n) + 63) & ~63)
enum { PSP_AUDIO_FORMAT_STEREO=0, PSP_AUDIO_NEXT_CHANNEL=-1, PSP_AUDIO_VOLUME_MAX=0x8000,
       PSP_THREAD_ATTR_USER=0, PSP_MODULE_AV_AVCODEC=0, PSP_MODULE_AV_MP3=1,
       GU_DIRECT=0, GU_PSM_8888=3, GU_SCISSOR_TEST=2, GU_DEPTH_TEST=1, GU_FALSE=0, GU_TRUE=1,
       GU_COLOR_BUFFER_BIT=1,
       GU_ALPHA_TEST=0, GU_BLEND=4, GU_CULL_FACE=5, GU_DITHER=6, GU_LIGHTING=17,
       GU_TEXTURE_2D=9, GU_NEAREST=0, GU_LINEAR=1, GU_CLAMP=1,
       GU_TFX_REPLACE=3, GU_TCC_RGBA=1, GU_SPRITES=6,
       GU_TEXTURE_32BITF=3, GU_VERTEX_32BITF=(3<<7), GU_TRANSFORM_2D=(1<<23),
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
inline unsigned& testGuDrawCalls();
inline unsigned& testGuCopyCalls();
struct TestDcacheCall { const void* address;unsigned bytes;bool invalidate;unsigned geCalls; };
inline bool& testDcacheTraceEnabled() { static bool enabled=false;return enabled; }
inline std::vector<TestDcacheCall>& testDcacheCalls() { static std::vector<TestDcacheCall> calls;return calls; }
inline void sceKernelDcacheWritebackRange(const void* address,unsigned bytes) {
    if(testDcacheTraceEnabled())testDcacheCalls().push_back({address,bytes,false,testGuDrawCalls()+testGuCopyCalls()});
}
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
inline void* sceGeEdramGetAddr() { alignas(16) static u32 vram[512*272*2]; return vram; }
// Optional buffered-renderer model: CPU VRAM and the displayed GPU surface are
// distinct. GE writes remain CPU-readable for capture, but CPU stores/cache
// writebacks alone cannot publish a new GPU image. Off for ordinary PSP tests.
inline bool& testGuSeparateFramebufferEnabled() { static bool enabled=false;return enabled; }
inline u32* testGuFramebuffer() { alignas(16) static u32 pixels[512*272*2];return pixels; }
inline u32* testGuFramebufferAlias(u32* cpu) {
    if(!testGuSeparateFramebufferEnabled())return 0;
    const uintptr_t base=(uintptr_t)sceGeEdramGetAddr(),address=(uintptr_t)cpu;
    if(address<base || address-base>=2*512*272*sizeof(u32))return 0;
    return testGuFramebuffer()+(address-base)/sizeof(u32);
}
inline uintptr_t& testDrawOffset() { static uintptr_t offset = 0; return offset; }
inline unsigned& testGuDrawCalls() { static unsigned value=0;return value; }
inline unsigned& testGuCopyCalls() { static unsigned value=0;return value; }
struct TestGuTexture {
    const u32* pixels=0;
    int width=0,height=0,stride=0,filter=GU_NEAREST;
    float scaleU=1.0f,scaleV=1.0f,offsetU=0.0f,offsetV=0.0f;
    bool enabled=false;
};
inline TestGuTexture& testGuTexture() { static TestGuTexture state;return state; }
inline int& testGuDrawStride() { static int stride=512;return stride; }
struct TestGuScissor { int left=0,top=0,right=480,bottom=272;bool enabled=true; };
inline TestGuScissor& testGuScissor() { static TestGuScissor state;return state; }
inline u32& testGuClearColor() { static u32 color=0;return color; }
inline void sceGuClearColor(u32 color) { testGuClearColor()=color; }
inline void sceGuClear(int mask) {
    assert(mask==GU_COLOR_BUFFER_BIT);
    const TestGuScissor& s=testGuScissor();
    u32* cpu=(u32*)((u8*)sceGeEdramGetAddr()+testDrawOffset());
    u32* gpu=testGuFramebufferAlias(cpu);
    for(int y=s.enabled?s.top:0;y<(s.enabled?s.bottom:272);++y)
        for(int x=s.enabled?s.left:0;x<(s.enabled?s.right:480);++x) {
            cpu[y*testGuDrawStride()+x]=testGuClearColor();
            if(gpu)gpu[y*testGuDrawStride()+x]=testGuClearColor();
        }
}
inline size_t& testGuMemoryUsed() { static size_t used=0;return used; }
inline void* sceGuGetMemory(int size) {
    alignas(16) static u8 memory[65536];
    assert(size>=0);
    const size_t aligned=((size_t)size+15u)&~(size_t)15u;
    size_t& used=testGuMemoryUsed();
    assert(aligned<=sizeof(memory)-used);
    void* result=memory+used;used+=aligned;return result;
}
inline void* sceGuSwapBuffers() {
    testDrawOffset() ^= 0x88000u;
    return (void*)testDrawOffset();
}
inline void sceKernelDcacheWritebackInvalidateRange(const void* address,unsigned bytes) {
    if(testDcacheTraceEnabled())testDcacheCalls().push_back({address,bytes,true,testGuDrawCalls()+testGuCopyCalls()});
}
inline void sceGuTexSync() {}
inline void sceGuCopyImage(int format,int sx,int sy,int w,int h,int sw,void* src,int dx,int dy,int dw,void* dst) {
    assert(format==GU_PSM_8888 && src && dst);
    assert(((uintptr_t)src&15u)==0 && ((uintptr_t)dst&15u)==0);
    assert(sw>0 && dw>0 && !(sw&15) && !(dw&15));
    assert(sx>=0 && sy>=0 && dx>=0 && dy>=0 && w>=0 && h>=0);
    assert(sx+w<=sw && dx+w<=dw);
    ++testGuCopyCalls();
    const u32* from=(const u32*)src;u32* to=(u32*)dst;
    u32* gpu=testGuFramebufferAlias(to);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int offset=(dy+y)*dw+dx+x;
        to[offset]=from[(sy+y)*sw+sx+x];
        if(gpu)gpu[offset]=to[offset];
    }
}
inline void sceGuInit() {}
inline void sceGuTerm() {}
inline void sceGuStart(int mode, void*) { assert(mode==GU_DIRECT);testGuMemoryUsed()=0; }
inline void sceGuDrawBufferList(int format,void* offset,int stride) {
    assert(format==GU_PSM_8888 && stride==512);
    assert((uintptr_t)offset==0 || (uintptr_t)offset==0x88000u);
    testDrawOffset()=(uintptr_t)offset;testGuDrawStride()=stride;
}
inline void sceGuDrawBuffer(int format,void* offset,int stride) { sceGuDrawBufferList(format,offset,stride); }
inline void sceGuDispBuffer(int, int, void*, int) {}
inline void sceGuDepthBuffer(void*, int) {}
inline void sceGuOffset(int, int) {}
inline void sceGuViewport(int, int, int, int) {}
inline void sceGuDepthRange(int, int) {}
inline void sceGuScissor(int left,int top,int right,int bottom) {
    assert(left>=0 && top>=0 && right<=480 && bottom<=272 && left<=right && top<=bottom);
    TestGuScissor& s=testGuScissor();s.left=left;s.top=top;s.right=right;s.bottom=bottom;
}
inline void sceGuEnable(int state) {
    if(state==GU_TEXTURE_2D)testGuTexture().enabled=true;
    if(state==GU_SCISSOR_TEST)testGuScissor().enabled=true;
}
inline void sceGuDisable(int state) {
    if(state==GU_TEXTURE_2D)testGuTexture().enabled=false;
    if(state==GU_SCISSOR_TEST)testGuScissor().enabled=false;
}
inline void sceGuTexMode(int format,int levels,int reserved,int swizzled) {
    assert(format==GU_PSM_8888 && levels==0 && reserved==0 && swizzled==0);
}
inline void sceGuTexImage(int level,int width,int height,int stride,const void* data) {
    assert(level==0 && width>0 && height>0 && width<=512 && height<=512);
    assert(!(width&(width-1)) && !(height&(height-1)));
    assert(stride>0 && stride<=512 && !(stride&15));
    assert(data && ((uintptr_t)data&15u)==0);
    TestGuTexture& t=testGuTexture();t.pixels=(const u32*)data;t.width=width;t.height=height;t.stride=stride;
}
inline void sceGuTexFunc(int function,int components) { assert(function==GU_TFX_REPLACE && components==GU_TCC_RGBA); }
inline void sceGuTexFilter(int minification,int magnification) {
    assert(minification==magnification && (minification==GU_NEAREST || minification==GU_LINEAR));
    testGuTexture().filter=minification;
}
inline void sceGuTexWrap(int u,int v) { assert(u==GU_CLAMP && v==GU_CLAMP); }
inline void sceGuTexScale(float u,float v) { testGuTexture().scaleU=u;testGuTexture().scaleV=v; }
inline void sceGuTexOffset(float u,float v) { testGuTexture().offsetU=u;testGuTexture().offsetV=v; }
inline void sceGuTexFlush() {}
inline u32 testGuTexel(int x,int y) {
    const TestGuTexture& t=testGuTexture();
    x=std::max(0,std::min(t.width-1,x));y=std::max(0,std::min(t.height-1,y));
    assert(x<t.stride);
    return t.pixels[(size_t)y*t.stride+x];
}
inline u32 testGuSample(double u,double v) {
    const TestGuTexture& t=testGuTexture();
    if(t.filter==GU_NEAREST)return testGuTexel((int)std::floor(u),(int)std::floor(v));
    const double x=u-0.5,y=v-0.5;
    const int left=(int)std::floor(x),top=(int)std::floor(y);
    const double fx=x-left,fy=y-top;
    const u32 pixels[]={testGuTexel(left,top),testGuTexel(left+1,top),testGuTexel(left,top+1),testGuTexel(left+1,top+1)};
    u32 result=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        const double a=((pixels[0]>>shift)&255)*(1-fx)+((pixels[1]>>shift)&255)*fx;
        const double b=((pixels[2]>>shift)&255)*(1-fx)+((pixels[3]>>shift)&255)*fx;
        result|=(u32)std::floor(a*(1-fy)+b*fy+0.5)<<shift;
    }
    return result;
}
inline void sceGuDrawArray(int primitive,int format,int count,const void* indices,const void* vertices) {
    assert(primitive==GU_SPRITES && format==(GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D));
    assert(count==2 && !indices && vertices && ((uintptr_t)vertices&15u)==0);
    const TestGuTexture& t=testGuTexture();assert(t.enabled && t.pixels);
    const float* v=(const float*)vertices; // Two vertices, each u,v,x,y,z.
    assert(v[7]>v[2] && v[8]>v[3]);
    const TestGuScissor& s=testGuScissor();
    const int left=std::max(s.enabled?s.left:0,(int)std::ceil(v[2]-0.5));
    const int top=std::max(s.enabled?s.top:0,(int)std::ceil(v[3]-0.5));
    const int right=std::min(s.enabled?s.right:480,(int)std::ceil(v[7]-0.5));
    const int bottom=std::min(s.enabled?s.bottom:272,(int)std::ceil(v[8]-0.5));
    u32* dst=(u32*)((u8*)sceGeEdramGetAddr()+testDrawOffset());
    u32* gpu=testGuFramebufferAlias(dst);
    ++testGuDrawCalls();
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
        const double u=(v[0]+((x+0.5-v[2])/(v[7]-v[2]))*(v[5]-v[0]))*t.scaleU+t.offsetU;
        const double w=(v[1]+((y+0.5-v[3])/(v[8]-v[3]))*(v[6]-v[1]))*t.scaleV+t.offsetV;
        dst[y*testGuDrawStride()+x]=testGuSample(u,w);
        if(gpu)gpu[y*testGuDrawStride()+x]=dst[y*testGuDrawStride()+x];
    }
}
inline void sceGuFinish() {}
inline void sceGuSync(int, int) {}
inline void sceGuDisplay(int) {}
#endif
