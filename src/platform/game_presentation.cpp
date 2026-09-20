#include "platform/game_presentation.h"
#include "platform/presentation.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <algorithm>
#include <stdint.h>
#include <string.h>

namespace n32 {
namespace {
u32 abgr(u32 c) {
    // ARGB -> BGRA -> ABGR. GCC emits Allegrex WSBW + ROR instead of
    // six mask/shift operations per pixel; alpha and every colour bit survive.
    const u32 reversed=__builtin_bswap32(c);
    return (reversed>>8)|(reversed<<24);
}
u32 powerOfTwo(u32 n) { u32 p=1; while(p<n)p<<=1; return p; }
struct TextureVertex { float u,v,x,y,z; };

// Center-aligned, clamped bilinear sampling for oversized software fallbacks.
// Eight fractional bits keep intermediates bounded and avoid floating point
// work inside the four-channel interpolation.
struct SampleAxis { u32 a,b,fraction; };
SampleAxis sampleAxis(u32 x,u32 source,u32 dest) {
    const int64_t position=(int64_t)(((u64)x*2+1)*source*128/dest)-128;
    if(position<=0)return {0,0,0};
    if(position>=(int64_t)(source-1)*256)return {source-1,source-1,0};
    return {(u32)(position>>8),(u32)(position>>8)+1,(u32)position&255u};
}
u32 blend(u32 a,u32 b,u32 c,u32 d,u32 fx,u32 fy) {
    u32 result=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        const u32 top=((a>>shift)&255u)*(256-fx)+((b>>shift)&255u)*fx;
        const u32 bottom=((c>>shift)&255u)*(256-fx)+((d>>shift)&255u)*fx;
        result|=((top*(256-fy)+bottom*fy+32768)>>16)<<shift;
    }
    return result;
}
}

u32* GamePresentation::pixels() {
    return reinterpret_cast<u32*>((reinterpret_cast<uintptr_t>(storage.data())+15u)&~uintptr_t(15u));
}
void GamePresentation::clear() {
    storage.clear();outputW=outputH=imageW=imageH=stride=textureH=0;
    textured=linear=dirty=false;
}
void GamePresentation::release() { clear();std::vector<u32>().swap(storage); }

bool GamePresentation::prepare(const std::vector<u32>& argb,u32 sourceW,u32 sourceH,
                               VideoScaling mode,bool smooth) {
    if(!sourceW || !sourceH || sourceW>argb.size()/sourceH) {clear();return false;}
    scaledSize(sourceW,sourceH,mode,&outputW,&outputH);
    const bool scaled=mode!=ScaleOriginal && (sourceW!=outputW || sourceH!=outputH);
    textured=scaled && sourceW<=512 && sourceH<=512;
    linear=smooth && scaled;
    imageW=textured?sourceW:outputW;imageH=textured?sourceH:outputH;
    stride=textured?std::max<u32>(16u,powerOfTwo(imageW)):(imageW+15u)&~15u;
    textureH=textured?powerOfTwo(imageH):imageH;
    storage.resize((size_t)stride*textureH+3); // Explicit 16-byte alignment on all allocators.
    u32* out=pixels();
    if(textured || !scaled) {
        // Snapshot dimensions so output stores cannot force member reloads in
        // the pixel loop, and advance pointers rather than rebuilding indexes.
        const u32 columns=imageW,rows=imageH,pitch=stride;
        const u32* source=argb.data();
        u32* row=out;
        for(u32 y=0;y<rows;++y,source+=sourceW,row+=pitch) {
            for(u32 x=0;x<columns;++x)row[x]=abgr(source[x]);
            // Bilinear sampling can reach one texel past the visible edge.
            // The rest of the power-of-two texture is never sampled.
            if(textured) {
                if(linear && columns<pitch)row[columns]=row[columns-1];
            } else std::fill(row+columns,row+pitch,row[columns-1]);
        }
    } else {
        SampleAxis columns[480];u32 nearest[480];
        for(u32 x=0;x<outputW;++x) {
            columns[x]=sampleAxis(x,sourceW,outputW);
            nearest[x]=(u32)(((u64)x*2+1)*sourceW/(2*outputW));
        }
        for(u32 y=0;y<outputH;++y) {
            u32* row=out+(size_t)y*stride;
            const SampleAxis ys=sampleAxis(y,sourceH,outputH);
            const u32 sy=(u32)(((u64)y*2+1)*sourceH/(2*outputH));
            for(u32 x=0;x<outputW;++x) {
                const SampleAxis xs=columns[x];
                row[x]=abgr(linear?blend(argb[(size_t)ys.a*sourceW+xs.a],argb[(size_t)ys.a*sourceW+xs.b],
                    argb[(size_t)ys.b*sourceW+xs.a],argb[(size_t)ys.b*sourceW+xs.b],xs.fraction,ys.fraction):
                    argb[(size_t)sy*sourceW+nearest[x]]);
            }
            std::fill(row+outputW,row+stride,row[outputW-1]);
        }
    }
    if(textured && linear && imageH<textureH)
        memcpy(out+(size_t)imageH*stride,out+(size_t)(imageH-1)*stride,
               (imageW+(imageW<stride?1:0))*sizeof(u32));
    dirty=true;
    return true;
}

void GamePresentation::draw(void* drawOffset,u32* drawBase,void* commandList,bool clearBackground) {
    if(empty())return;
    u32* src=pixels();
    if(dirty) {
        const u32 rows=imageH+(textured && linear && imageH<textureH?1:0);
        sceKernelDcacheWritebackRange(src,stride*rows*sizeof(u32));dirty=false;
    }
    // Publish CPU background clears and discard aliases before GE writes. A
    // sync below makes capture and reuse of this texture safe.
    sceKernelDcacheWritebackInvalidateRange(drawBase,272*512*sizeof(u32));
    sceGuStart(GU_DIRECT,commandList);
    if(clearBackground) {
        // Updating CPU VRAM alone does not clear a cached GPU framebuffer.
        sceGuDrawBufferList(GU_PSM_8888,drawOffset,512);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT);
    }
    const int dx=(480-outputW)/2,dy=(272-outputH)/2;
    if(textured) {
        sceGuDrawBufferList(GU_PSM_8888,drawOffset,512);
        sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(GU_PSM_8888,0,0,0);
        sceGuTexImage(0,stride,textureH,stride,src);
        sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);
        sceGuTexFilter(linear?GU_LINEAR:GU_NEAREST,linear?GU_LINEAR:GU_NEAREST);
        sceGuTexWrap(GU_CLAMP,GU_CLAMP);
        sceGuTexScale(1.0f,1.0f);sceGuTexOffset(0.0f,0.0f);sceGuTexFlush();
        // A strip endpoint is a float: 320 -> 480 can otherwise put an exact
        // texel boundary just below its integer (e.g. 96.9999998). This bias is
        // smaller than the nearest distinct sample-boundary gap, 1/(2*480),
        // while exceeding float error across our <=512-texel coordinates.
        const float bias=linear?0.0f:1.0f/4096.0f;
        // Narrow strips keep texture cache locality on the PSP's GE.
        for(u32 x=0;x<outputW;x+=32) {
            const u32 right=std::min(x+32,outputW);
            TextureVertex* vertices=static_cast<TextureVertex*>(sceGuGetMemory(2*sizeof(TextureVertex)));
            vertices[0]={(float)x*imageW/outputW+bias,bias,(float)(dx+x),(float)dy,0.0f};
            vertices[1]={(float)right*imageW/outputW+bias,(float)imageH+bias,(float)(dx+right),(float)(dy+outputH),0.0f};
            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,vertices);
        }
        sceGuDisable(GU_TEXTURE_2D);
    } else {
        // Buffer pitch is aligned independently of visible width (e.g. 362).
        sceGuCopyImage(GU_PSM_8888,0,0,outputW,outputH,stride,src,dx,dy,512,drawBase);
        sceGuTexSync();
    }
    sceGuFinish();sceGuSync(0,0);
}
}
