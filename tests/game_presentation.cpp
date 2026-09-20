#include "platform/game_presentation.h"
#include "psp_test_api.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace n32;
namespace {
const u32 kBlack=0xff000000u;
const u32 kUntouched=0x4d2913abu;
const size_t kPagePixels=512u*272u;
const size_t kStorageLimit=1024u*1024u+64u; // Includes allocator alignment slop.
alignas(16) unsigned commandList[4096];

u32 toScreen(u32 argb) {
    const u32 alpha=argb>>24,red=(argb>>16)&255,green=(argb>>8)&255,blue=argb&255;
    return alpha*0x1000000u+blue*0x10000u+green*0x100u+red;
}

// This reference samples the original ARGB image directly in double precision.
// It does not use the presenter, its padded texture or the host GU sampler.
u32 reference(const std::vector<u32>& source,unsigned sw,unsigned sh,
              unsigned x,unsigned y,unsigned dw,unsigned dh,VideoScaling mode,bool smooth) {
    if(mode==ScaleOriginal)return toScreen(source[(size_t)y*sw+x]);
    if(!smooth) {
        const unsigned sx=(unsigned)(((u64)x*2+1)*sw/(2*dw));
        const unsigned sy=(unsigned)(((u64)y*2+1)*sh/(2*dh));
        return toScreen(source[(size_t)sy*sw+sx]);
    }
    const double sx=std::max(0.0,std::min((double)sw-1,(x+0.5)*sw/dw-0.5));
    const double sy=std::max(0.0,std::min((double)sh-1,(y+0.5)*sh/dh-0.5));
    const unsigned x0=(unsigned)std::floor(sx),y0=(unsigned)std::floor(sy);
    const unsigned xs[]={x0,std::min(x0+1,sw-1)},ys[]={y0,std::min(y0+1,sh-1)};
    const double wx[]={1-(sx-x0),sx-x0},wy[]={1-(sy-y0),sy-y0};
    u32 argb=0;
    for(unsigned channel=0;channel<4;++channel) {
        double value=0;
        for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i)
            value+=((source[(size_t)ys[j]*sw+xs[i]]>>(channel*8))&255)*wx[i]*wy[j];
        argb|=(u32)std::floor(value+0.5)<<(channel*8);
    }
    return toScreen(argb);
}

void expectPixel(u32 actual,u32 expected,unsigned tolerance,const char* name,unsigned x,unsigned y) {
    for(unsigned channel=0;channel<4;++channel) {
        const int difference=(int)((actual>>(channel*8))&255)-(int)((expected>>(channel*8))&255);
        if(std::abs(difference)>(int)tolerance) {
            std::fprintf(stderr,"%s at (%u,%u): got %08x expected %08x tolerance %u\n",
                         name,x,y,(unsigned)actual,(unsigned)expected,tolerance);
            assert(false);
        }
    }
}

std::vector<u32> coordinates(unsigned w,unsigned h) {
    std::vector<u32> pixels((size_t)w*h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        pixels[(size_t)y*w+x]=((128u+(x+y)%128u)<<24)|(((x*17+y*3)&255)<<16)|
                              (((x*7+y*29)&255)<<8)|((x*31+y*11)&255);
    return pixels;
}

std::vector<u32> gradient(unsigned w,unsigned h) {
    std::vector<u32> pixels((size_t)w*h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const unsigned red=x*255/std::max(1u,w-1),green=y*255/std::max(1u,h-1);
        const unsigned blue=(red+green)/2,alpha=128+green/2;
        pixels[(size_t)y*w+x]=(alpha<<24)|(red<<16)|(green<<8)|blue;
    }
    return pixels;
}

void verifyTarget(GamePresentation& presenter,const std::vector<u32>& source,unsigned sw,unsigned sh,
                  VideoScaling mode,bool smooth,bool texture,unsigned slot,const char* name) {
    u32* vram=(u32*)sceGeEdramGetAddr();
    std::fill(vram,vram+kPagePixels*2,kUntouched);
    u32* target=vram+slot*kPagePixels;
    for(unsigned y=0;y<272;++y)std::fill(target+y*512,target+y*512+480,kBlack);
    const unsigned w=presenter.width(),h=presenter.height(),dx=(480-w)/2,dy=(272-h)/2;
    testGuDrawCalls()=testGuCopyCalls()=0;
    // Deliberately start on the other draw page: the textured path must bind its
    // explicit target, and the copy path must use the supplied destination.
    testDrawOffset()=(slot^1u)*0x88000u;
    presenter.draw((void*)(uintptr_t)(slot*0x88000u),target,commandList);
    assert(testGuDrawCalls()==(texture?(w+31)/32:0));
    assert(testGuCopyCalls()==(texture?0:1));
    if(texture) {
        assert(testDrawOffset()==slot*0x88000u);
        assert(testGuTexture().filter==(smooth?GU_LINEAR:GU_NEAREST));
        assert(!testGuTexture().enabled);
        assert((uintptr_t)sceGuSwapBuffers()==(slot^1u)*0x88000u);
    }
    for(unsigned y=0;y<272;++y)for(unsigned x=0;x<512;++x) {
        const bool inside=x>=dx && x<dx+w && y>=dy && y<dy+h;
        const u32 expected=inside?reference(source,sw,sh,x-dx,y-dy,w,h,mode,smooth):
                                   (x<480?kBlack:kUntouched);
        expectPixel(target[y*512+x],expected,inside && smooth?1:0,name,x,y);
        assert(vram[(slot^1u)*kPagePixels+y*512+x]==kUntouched);
    }
}

void check(const char* name,const std::vector<u32>& source,unsigned sw,unsigned sh,
           VideoScaling mode,bool smooth,unsigned w,unsigned h,bool texture) {
    GamePresentation presenter;
    assert(presenter.prepare(source,sw,sh,mode,smooth));
    assert(!presenter.empty() && presenter.width()==w && presenter.height()==h);
    assert(presenter.usesTexture()==texture && presenter.retainedBytes()<=kStorageLimit);
    verifyTarget(presenter,source,sw,sh,mode,smooth,texture,0,name);
    verifyTarget(presenter,source,sw,sh,mode,smooth,texture,1,name);
}

void geometryAndSampling() {
    // The coordinate pattern makes an off-by-one texel visible, including at
    // every 32-pixel strip boundary and exact integer sampling ties.
    const std::vector<u32> pixels=coordinates(320,240);
    check("320 FIT sharp",pixels,320,240,ScaleFit,false,362,272,true);
    check("320 FULL sharp",pixels,320,240,ScaleFull,false,480,272,true);
    check("320 original",pixels,320,240,ScaleOriginal,false,320,240,false);
    check("original ignores smoothing",pixels,320,240,ScaleOriginal,true,320,240,false);
    check("317 FIT sharp",coordinates(317,239),317,239,ScaleFit,false,360,272,true);
    check("317 FULL sharp",coordinates(317,239),317,239,ScaleFull,false,480,272,true);
    check("512 square sharp",coordinates(512,512),512,512,ScaleFit,false,272,272,true);
    check("native PSP canvas",coordinates(480,272),480,272,ScaleFit,false,480,272,false);

    const std::vector<u32> corners={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu};
    check("2x2 four corners sharp",corners,2,2,ScaleFull,false,480,272,true);
    check("2x2 four corners smooth",corners,2,2,ScaleFull,true,480,272,true);
    check("320 FIT gradient",gradient(320,240),320,240,ScaleFit,true,362,272,true);
    check("317 FULL gradient",gradient(317,239),317,239,ScaleFull,true,480,272,true);
    check("512 square gradient",gradient(512,512),512,512,ScaleFit,true,272,272,true);
}

void channelBits() {
    // Exercise every colour/alpha bit separately, its complement, and arbitrary
    // combinations against the independent byte-channel reference above.
    std::vector<u32> pixels(64*64);
    for(unsigned bit=0;bit<32;++bit) {
        pixels[bit]=u32(1)<<bit;
        pixels[32+bit]=~(u32(1)<<bit);
    }
    u32 state=0x1248abcd;
    for(size_t i=64;i<pixels.size();++i) {
        state=state*1664525u+1013904223u;
        pixels[i]=state;
    }
    check("all channel bits original",pixels,64,64,ScaleOriginal,false,64,64,false);
    check("all channel bits texture",pixels,64,64,ScaleFull,false,480,272,true);
}

void paddingAndThinImages() {
    // Every channel is nonzero. A single uninitialised or black padding texel
    // causes a detectable seam on the last row/column with linear filtering.
    const u32 solid=0xc95dabf1u;
    check("1x1 clamp",std::vector<u32>(1,solid),1,1,ScaleFit,true,272,272,true);
    check("1x2 thin",std::vector<u32>(2,solid),1,2,ScaleFit,true,136,272,true);
    check("2x1 thin",std::vector<u32>(2,solid),2,1,ScaleFit,true,480,240,true);
    check("317x239 padding",std::vector<u32>(317*239,solid),317,239,ScaleFull,true,480,272,true);
    check("511x511 padding",std::vector<u32>(511*511,solid),511,511,ScaleFull,true,480,272,true);
    check("512x1 texture boundary",std::vector<u32>(512,solid),512,1,ScaleFull,true,480,272,true);
    check("1x512 texture boundary",std::vector<u32>(512,solid),1,512,ScaleFull,true,480,272,true);
}

void oversizedFallback() {
    check("640 FIT sharp fallback",coordinates(640,480),640,480,ScaleFit,false,362,272,false);
    check("640 FULL sharp fallback",coordinates(640,480),640,480,ScaleFull,false,480,272,false);
    check("640 original crop",coordinates(640,480),640,480,ScaleOriginal,false,480,272,false);
    check("640 FIT smooth fallback",gradient(640,480),640,480,ScaleFit,true,362,272,false);
    check("640 FULL smooth fallback",gradient(640,480),640,480,ScaleFull,true,480,272,false);
    check("513 wide sharp fallback",coordinates(513,239),513,239,ScaleFit,false,480,223,false);
    check("513 high smooth fallback",gradient(317,513),317,513,ScaleFit,true,168,272,false);
    check("513x1 smooth fallback",std::vector<u32>(513,0xf1234567u),513,1,ScaleFull,true,480,272,false);
    check("1x513 smooth fallback",std::vector<u32>(513,0xf1234567u),1,513,ScaleFull,true,480,272,false);
}

void lifetimeAndInvalidInput() {
    GamePresentation presenter;
    assert(presenter.empty() && presenter.width()==0 && presenter.height()==0 && presenter.retainedBytes()==0);
    const std::vector<u32> small=coordinates(317,239),large=gradient(512,512);
    assert(presenter.prepare(small,317,239,ScaleFull,false));
    for(unsigned i=0;i<12;++i) {
        assert(presenter.prepare(large,512,512,ScaleFit,i&1));
        assert(presenter.retainedBytes()<=kStorageLimit);
        assert(presenter.prepare(small,317,239,ScaleFull,i&1));
        assert(presenter.retainedBytes()<=kStorageLimit);
    }
    verifyTarget(presenter,small,317,239,ScaleFull,true,true,1,"reused storage");
    const size_t retained=presenter.retainedBytes();
    presenter.clear();
    assert(presenter.empty() && presenter.width()==0 && presenter.height()==0 && !presenter.usesTexture());
    assert(presenter.retainedBytes()==retained);
    testGuDrawCalls()=testGuCopyCalls()=0;
    presenter.draw(0,(u32*)sceGeEdramGetAddr(),commandList);
    assert(testGuDrawCalls()==0 && testGuCopyCalls()==0);
    assert(!presenter.prepare(small,0,239,ScaleFull,false));
    assert(!presenter.prepare(small,317,0,ScaleFull,false));
    assert(!presenter.prepare(std::vector<u32>(),1,1,ScaleFit,false));
    assert(!presenter.prepare(std::vector<u32>(5),3,2,ScaleFull,true));
    assert(!presenter.prepare(small,0xffffffffu,0xffffffffu,ScaleFull,true));
    assert(presenter.empty() && presenter.width()==0 && presenter.height()==0);
    assert(presenter.prepare(small,317,239,ScaleFit,false));
    assert(!presenter.prepare(std::vector<u32>(5),3,2,ScaleFull,true));
    assert(presenter.empty() && presenter.width()==0 && presenter.height()==0 && !presenter.usesTexture());
    assert(presenter.prepare(small,317,239,ScaleFit,false));
    verifyTarget(presenter,small,317,239,ScaleFit,false,true,0,"recovery after invalid input");
    presenter.release();
    assert(presenter.empty() && presenter.retainedBytes()==0 && presenter.width()==0 && presenter.height()==0);
    assert(presenter.prepare(small,317,239,ScaleOriginal,false));
    verifyTarget(presenter,small,317,239,ScaleOriginal,false,false,1,"recovery after release");
}
}

int main() {
    geometryAndSampling();
    channelBits();
    paddingAndThinImages();
    oversizedFallback();
    lifetimeAndInvalidInput();
    std::puts("PASS: GE texture filtering/strips, colour and aspect, padded edges, aligned oversized fallbacks, double buffers and bounded presentation storage");
}
