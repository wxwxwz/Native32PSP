#include "core/renderer.h"
#include <algorithm>

namespace n32 {

Renderer::Renderer() : screenX(0), screenY(0), width(320), height(240) {
    buffer.assign(width * height, 0xff000000u);
}

Renderer::Renderer(u32 widthValue, u32 heightValue) : screenX(0), screenY(0), width(0), height(0) {
    resize(widthValue, heightValue);
}

void Renderer::resize(u32 widthValue, u32 heightValue) {
    width = widthValue;
    height = heightValue;
    buffer.assign((size_t)width * (size_t)height, 0xff000000u);
}

void Renderer::clearSpriteOverrides() {
    spriteOverrides.clear();
}

void Renderer::setSpriteOverride(const std::string& name, const RgbaImage& image, const std::string& visibilityLeader) {
    SpriteOverride overrideImage;
    overrideImage.image = image;
    overrideImage.hasVisibilityLeader = true;
    overrideImage.visibilityLeader = visibilityLeader;
    spriteOverrides[name] = overrideImage;
}

void Renderer::setSpriteOverride(const std::string& name, const RgbaImage& image) {
    SpriteOverride overrideImage;
    overrideImage.image = image;
    overrideImage.hasVisibilityLeader = false;
    spriteOverrides[name] = overrideImage;
}

size_t Renderer::spriteOverrideCount() const {
    return spriteOverrides.size();
}

void Renderer::drawFrame(Native32Reader* reader, const SpriteSystem& sprites, const std::vector<FrameObject>& curFrame) {
    std::fill(buffer.begin(), buffer.end(), 0xff000000u);
    if (!reader) {
        return;
    }

    // Keep the allocation from the first frame and only rebuild entries.
    drawList.clear();
    drawList.reserve(curFrame.size() + sprites.sprites.size());
    size_t order = 0;

    for (size_t i = 0; i < curFrame.size(); ++i) {
        const FrameObject& obj = curFrame[i];
        if (obj.type == ObjectImage) {
            DrawEntry entry;
            entry.sourceType = DrawImage;
            entry.overrideImage = 0;
            entry.imageIndex = obj.index;
            entry.x = obj.x;
            entry.y = obj.y;
            entry.depth = obj.depth;
            entry.order = order++;
            drawList.push_back(entry);
        }
    }

    for (SpriteMap::const_iterator it = sprites.sprites.begin(); it != sprites.sprites.end(); ++it) {
        const std::string& name = it->first;
        const MovieState& movie = it->second;
        if (!movie.visible) {
            continue;
        }

        std::map<std::string, SpriteOverride>::const_iterator overrideIt = spriteOverrides.find(name);
        if (overrideIt != spriteOverrides.end()) {
            const SpriteOverride& overrideImage = overrideIt->second;
            if (overrideImage.hasVisibilityLeader) {
                const MovieState* leader = sprites.get(overrideImage.visibilityLeader);
                if (leader && !leader->visible) {
                    continue;
                }
            }

            const std::vector<MovieFrame>* movieFrames = reader->getMovieRef(movie.movie);
            s32 frameX = 0;
            s32 frameY = 0;
            if (movieFrames && movie.frame < movieFrames->size()) {
                frameX = (*movieFrames)[movie.frame].x;
                frameY = (*movieFrames)[movie.frame].y;
            }

            DrawEntry entry;
            entry.sourceType = DrawOverride;
            // Overrides are stable map entries for this draw call. Carry the
            // image directly so sorting never copies names or repeats lookups.
            entry.overrideImage = &overrideImage.image;
            entry.imageIndex = 0;
            entry.x = movie.x + frameX;
            entry.y = movie.y + frameY;
            entry.depth = movie.depth;
            entry.order = order++;
            drawList.push_back(entry);
            continue;
        }

        const std::vector<MovieFrame>* movieFrames = reader->getMovieRef(movie.movie);
        if (movieFrames && movie.frame < movieFrames->size()) {
            const MovieFrame& frame = (*movieFrames)[movie.frame];
            DrawEntry entry;
            entry.sourceType = DrawImage;
            entry.overrideImage = 0;
            entry.imageIndex = frame.image;
            entry.x = movie.x + frame.x;
            entry.y = movie.y + frame.y;
            entry.depth = movie.depth;
            entry.order = order++;
            drawList.push_back(entry);
        }
    }

    struct DrawEntryLess {
        bool operator()(const DrawEntry& a, const DrawEntry& b) const {
            if (a.depth != b.depth) {
                return a.depth < b.depth;
            }
            return a.order < b.order;
        }
    };
    std::sort(drawList.begin(), drawList.end(), DrawEntryLess());

    for (size_t i = 0; i < drawList.size(); ++i) {
        const DrawEntry& entry = drawList[i];
        s32 x = entry.x + screenX;
        s32 y = entry.y + screenY;
        if (entry.sourceType == DrawImage) {
            const RgbaImage* image = reader->getImageRef(entry.imageIndex);
            if (image) {
                blitImage(*image, x, y);
            }
        } else {
            if (entry.overrideImage) {
                blitImage(*entry.overrideImage, x, y);
            }
        }
    }
}

void Renderer::blitImage(const RgbaImage& image, s32 dstX, s32 dstY) {
    if (image.width == 0 || image.height == 0 || width == 0 || height == 0 || buffer.empty()) {
        return;
    }

    // Clip the rectangle once before entering the inner loop. The previous
    // implementation performed two coordinate checks and a source-index
    // bounds check for every pixel, which is a significant cost on Allegrex.
    // Reject wholly offscreen coordinates before negation/subtraction, which
    // otherwise overflow for script-controlled INT_MIN/INT_MAX offsets.
    if ((s64)dstX >= (s64)width || (s64)dstY >= (s64)height ||
        (s64)dstX + image.width <= 0 || (s64)dstY + image.height <= 0 ||
        image.width > 0x7fffffffu || image.height > 0x7fffffffu ||
        width > 0x7fffffffu || height > 0x7fffffffu) return;
    const s32 bufferWidth = (s32)width;
    const s32 bufferHeight = (s32)height;
    const s32 srcX0 = std::max<s32>(0, -dstX);
    const s32 srcY0 = std::max<s32>(0, -dstY);
    const s32 srcX1 = (s32)std::min<s64>(image.width, (s64)bufferWidth - dstX);
    const s32 srcY1 = (s32)std::min<s64>(image.height, (s64)bufferHeight - dstY);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) {
        return;
    }

    const size_t copyWidth = (size_t)(srcX1 - srcX0);
    const size_t dstX0 = (size_t)(dstX + srcX0);
    for (s32 sy = srcY0; sy < srcY1; ++sy) {
        const s32 dy = dstY + sy;
        const size_t srcStart = (size_t)sy * (size_t)image.width + (size_t)srcX0;
        if (srcStart >= image.pixels.size()) {
            break;
        }
        const size_t dstStart = (size_t)dy * (size_t)width + dstX0;
        if (dstStart >= buffer.size()) {
            break;
        }
        const size_t rowWidth = std::min(copyWidth, std::min(image.pixels.size() - srcStart,
                                                              buffer.size() - dstStart));
        const u32* src = &image.pixels[srcStart];
        u32* dst = &buffer[dstStart];

        // Preserve the original compositing rule: any non-zero alpha pixel
        // replaces the destination, while fully transparent pixels are left
        // untouched. The clipped row means there are no bounds checks here.
        for (size_t i = 0; i < rowWidth; ++i) {
            if ((src[i] & 0xff000000u) != 0) {
                dst[i] = src[i];
            }
        }
    }
}

}
