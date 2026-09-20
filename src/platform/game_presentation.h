#ifndef N32_GAME_PRESENTATION_H
#define N32_GAME_PRESENTATION_H
#include "platform/psp_settings.h"
#include <vector>
#include <stddef.h>

namespace n32 {
// Keeps one aligned ABGR surface. Common scaled canvases stay at native
// resolution; the GE samples them directly instead of CPU-scaling every frame.
class GamePresentation {
public:
    bool prepare(const std::vector<u32>& argb, u32 sourceW, u32 sourceH,
                 VideoScaling mode, bool smooth);
    void draw(void* drawOffset, u32* drawBase, void* commandList, bool clearBackground=false);
    void clear();
    void release();
    bool empty() const { return storage.empty(); }
    u32 width() const { return outputW; }
    u32 height() const { return outputH; }
    bool usesTexture() const { return textured; }
    size_t retainedBytes() const { return storage.capacity() * sizeof(u32); }
private:
    u32* pixels();
    std::vector<u32> storage;
    u32 outputW=0, outputH=0, imageW=0, imageH=0, stride=0, textureH=0;
    bool textured=false, linear=false, dirty=false;
};
}
#endif
