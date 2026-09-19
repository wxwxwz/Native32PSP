#ifndef N32_PRESENTATION_H
#define N32_PRESENTATION_H
#include "platform/psp_settings.h"
namespace n32 {
inline void scaledSize(u32 w,u32 h,VideoScaling mode,u32* outW,u32* outH) {
    if(!w || !h) {*outW=*outH=0;return;}
    if(mode==ScaleFull) {*outW=480;*outH=272;}
    else if(mode==ScaleFit) {
        if((u64)w*272>(u64)h*480) {*outW=480;*outH=(u32)((u64)h*480/w);}
        else {*outH=272;*outW=(u32)((u64)w*272/h);}
        if(!*outW)*outW=1;
        if(!*outH)*outH=1;
    } else {*outW=w>480?480:w;*outH=h>272?272:h;}
}
class DisplayFps {
public:
    DisplayFps(){reset();}
    void reset(){started=false;last=frames=value=0;}
    bool sample(u32 now) {
        if(!started){started=true;last=now;return false;}
        u32 elapsed=now-last;
        if(elapsed<1000000)return false;
        value=(u32)(((u64)frames*1000000+elapsed/2)/elapsed);
        frames=0;last=now;return true;
    }
    void presented(){++frames;}
    u32 value;
private:
    bool started;u32 last,frames;
};
}
#endif
