#include "core/renderer.h"
#include <algorithm>
#include <cstring>

namespace n32 {

static bool intersectsCanvas(s64 x, s64 y, u32 imageWidth, u32 imageHeight, u32 width, u32 height) {
    return imageWidth && imageHeight && width && height &&
           imageWidth <= 0x7fffffffu && imageHeight <= 0x7fffffffu &&
           width <= 0x7fffffffu && height <= 0x7fffffffu &&
           x < (s64)width && y < (s64)height && x + imageWidth > 0 && y + imageHeight > 0;
}

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
    overrideImage.drawInfo = imageDrawInfo(image);
    overrideImage.hasVisibilityLeader = true;
    overrideImage.visibilityLeader = visibilityLeader;
    spriteOverrides[name] = overrideImage;
}

void Renderer::setSpriteOverride(const std::string& name, const RgbaImage& image) {
    SpriteOverride overrideImage;
    overrideImage.image = image;
    overrideImage.drawInfo = imageDrawInfo(image);
    overrideImage.hasVisibilityLeader = false;
    spriteOverrides[name] = overrideImage;
}

size_t Renderer::spriteOverrideCount() const {
    return spriteOverrides.size();
}

void Renderer::drawFrame(Native32Reader* reader, const SpriteSystem& sprites, const std::vector<FrameObject>& curFrame) {
    if (!reader) {
        std::fill(buffer.begin(), buffer.end(), 0xff000000u);
        return;
    }

    // Keep the allocation from the first frame and only rebuild entries.
    drawList.clear();
    drawList.reserve(curFrame.size() + sprites.sprites.size());
    size_t order = 0;

    for (size_t i = 0; i < curFrame.size(); ++i) {
        const FrameObject& obj = curFrame[i];
        if (obj.type == ObjectImage) {
            u32 imageWidth, imageHeight;
            // Reject offscreen map tiles before sorting. Dimensions are a
            // read-only lookup: decoding and LRU touches stay in draw order.
            if (!reader->getImageDimensions(obj.index, &imageWidth, &imageHeight) ||
                !intersectsCanvas((s64)obj.x + screenX, (s64)obj.y + screenY,
                                  imageWidth, imageHeight, width, height)) continue;
            DrawEntry entry;
            entry.sourceType = DrawImage;
            entry.overrideImage = 0;
            entry.overrideInfo = 0;
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
            if (!intersectsCanvas((s64)movie.x + frameX + screenX,
                                  (s64)movie.y + frameY + screenY,
                                  overrideImage.image.width, overrideImage.image.height,
                                  width, height)) continue;

            DrawEntry entry;
            entry.sourceType = DrawOverride;
            // Overrides are stable map entries for this draw call. Carry the
            // image directly so sorting never copies names or repeats lookups.
            entry.overrideImage = &overrideImage.image;
            entry.overrideInfo = &overrideImage.drawInfo;
            entry.imageIndex = 0;
            entry.x = (s64)movie.x + frameX;
            entry.y = (s64)movie.y + frameY;
            entry.depth = movie.depth;
            entry.order = order++;
            drawList.push_back(entry);
            continue;
        }

        const std::vector<MovieFrame>* movieFrames = reader->getMovieRef(movie.movie);
        if (movieFrames && movie.frame < movieFrames->size()) {
            const MovieFrame& frame = (*movieFrames)[movie.frame];
            u32 imageWidth, imageHeight;
            if (!reader->getImageDimensions(frame.image, &imageWidth, &imageHeight) ||
                !intersectsCanvas((s64)movie.x + frame.x + screenX,
                                  (s64)movie.y + frame.y + screenY,
                                  imageWidth, imageHeight, width, height)) continue;
            DrawEntry entry;
            entry.sourceType = DrawImage;
            entry.overrideImage = 0;
            entry.overrideInfo = 0;
            entry.imageIndex = frame.image;
            entry.x = (s64)movie.x + frame.x;
            entry.y = (s64)movie.y + frame.y;
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

    bool initialized = false;
    for (size_t i = 0; i < drawList.size(); ++i) {
        const DrawEntry& entry = drawList[i];
        s64 x = entry.x + screenX;
        s64 y = entry.y + screenY;
        const RgbaImage* image = 0;
        const ImageDrawInfo* info = 0;
        if (entry.sourceType == DrawImage) {
            // Entry bounds were checked before sorting. This synchronous draw
            // has no callbacks that change offsets or immutable asset sizes;
            // an eviction/redecode keeps those dimensions. Touch LRU only here.
            image = reader->getImageRef(entry.imageIndex, 0, &info);
        } else {
            image = entry.overrideImage;
            info = entry.overrideInfo;
        }
        if (!image || !info || info->left >= info->right || info->top >= info->bottom ||
            x + info->left >= width || y + info->top >= height ||
            x + info->right <= 0 || y + info->bottom <= 0) continue;
        if (!initialized) {
            // Only a complete, opaque first layer can replace the black clear.
            // Partial/truncated images, holes and empty scenes still clear.
            const bool coversCanvas = info->allPixelsVisible && x <= 0 && y <= 0 &&
                x + image->width >= width && y + image->height >= height &&
                (u64)width * height == buffer.size();
            if (!coversCanvas) std::fill(buffer.begin(), buffer.end(), 0xff000000u);
            initialized = true;
        }
        blitImage(*image, (s32)x, (s32)y, *info);
    }
    if (!initialized) std::fill(buffer.begin(), buffer.end(), 0xff000000u);
}

void Renderer::blitImage(const RgbaImage& image, s32 dstX, s32 dstY) {
    // Public callers may mutate their pixels between calls. Only stable reader
    // cache entries and owned sprite overrides carry precomputed pixel bounds.
    ImageDrawInfo info;
    info.right = image.width;
    info.bottom = image.height;
    blitImage(image, dstX, dstY, info);
}

void Renderer::blitImage(const RgbaImage& image, s32 dstX, s32 dstY, const ImageDrawInfo& info) {
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
    const s32 srcX0 = std::max<s32>((s32)info.left, -dstX);
    const s32 srcY0 = std::max<s32>((s32)info.top, -dstY);
    const s32 srcX1 = (s32)std::min<s64>(info.right, (s64)bufferWidth - dstX);
    const s32 srcY1 = (s32)std::min<s64>(info.bottom, (s64)bufferHeight - dstY);
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

        if (info.allPixelsVisible) {
            std::memcpy(dst, src, rowWidth * sizeof(u32));
            continue;
        }

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
