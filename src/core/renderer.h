#ifndef NATIVE32_RENDERER_H
#define NATIVE32_RENDERER_H

#include "core/native32_reader.h"
#include "core/sprite_system.h"

namespace n32 {

class Renderer {
public:
    Renderer();
    Renderer(u32 widthValue, u32 heightValue);

    void resize(u32 widthValue, u32 heightValue);
    void clearSpriteOverrides();
    void setSpriteOverride(const std::string& name, const RgbaImage& image, const std::string& visibilityLeader);
    void setSpriteOverride(const std::string& name, const RgbaImage& image);
    size_t spriteOverrideCount() const;

    void drawFrame(Native32Reader* reader, const SpriteSystem& sprites, const std::vector<FrameObject>& curFrame);
    void blitImage(const RgbaImage& image, s32 dstX, s32 dstY);

    s32 screenX;
    s32 screenY;
    std::vector<u32> buffer;
    u32 width;
    u32 height;

private:
    enum DrawSourceType {
        DrawImage,
        DrawOverride
    };

    struct DrawEntry {
        DrawSourceType sourceType;
        u32 imageIndex;
        const RgbaImage* overrideImage;
        s32 x;
        s32 y;
        u32 depth;
        size_t order;
    };

    struct SpriteOverride {
        RgbaImage image;
        bool hasVisibilityLeader;
        std::string visibilityLeader;
    };

    std::map<std::string, SpriteOverride> spriteOverrides;
    // Reused across frames to avoid allocating the draw list at 30 Hz.
    std::vector<DrawEntry> drawList;
};

}

#endif
