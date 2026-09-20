#include "core/image_decoder.h"
#include "core/endian.h"
#include "core/raster_limits.h"
#include <algorithm>

namespace n32 {

static u8 clip(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (u8)v;
}

bool decodeImageYuv(const std::vector<u8>& data, RgbaImage* out) {
    return decodeImageYuv(data.data(), data.size(), out);
}

// Preserve the format's zero-chroma replacement: vertical neighbors are
// selected first, then horizontal neighbors from that completed row. Two
// short rows replace the four full-image intermediate interpolation buffers.
static void chromaRow(const std::vector<u8>& plane, size_t width, size_t height,
                      size_t outputY, u8* row) {
    const size_t y = outputY / 2;
    const size_t neighbor = (outputY & 1) ? std::min(y + 1, height - 1) : (y ? y - 1 : 0);
    const u8* current = plane.data() + y * width;
    const u8* adjacent = plane.data() + neighbor * width;
    for (size_t x = 0; x < width; ++x) row[x] = current[x] ? current[x] : adjacent[x];
}

bool decodeImageYuv(const u8* data, size_t size, RgbaImage* out, ImageDrawInfo* info) {
    if (!out || !data || size < 8) {
        return false;
    }
    size_t width = read_u16_le(&data[0], 0);
    size_t height = read_u16_le(&data[0], 2);
    size_t imageSize = read_u32_le(&data[0], 4);
    if (!validRasterSize((u32)width, (u32)height) || imageSize > size - 8) {
        return false;
    }

    std::vector<u8> y22(width * height, 0);
    size_t uvW = (width + 1) / 2;
    size_t uvH = (height + 1) / 2;
    std::vector<u8> u11(uvW * uvH, 0);
    std::vector<u8> v11(uvW * uvH, 0);

    size_t pixel = 0;
    size_t pos = 8;
    size_t maxPixels = uvW * uvH;
    size_t limit = imageSize + 8;

    while (pos + 2 <= limit && pixel < maxPixels) {
        u16 op = read_u16_le(&data[0], pos);
        if (op == 0) {
            return false;
        }
        pos += 2;
        size_t count = (op & 0x8000) ? (op & 0x7fff) : op;

        if (op & 0x8000) {
            for (size_t n = 0; n < count && pos + 6 <= limit && pixel < maxPixels; ++n) {
                size_t yy = pixel / uvW;
                size_t xx = pixel % uvW;
                if (2 * xx < width && 2 * yy < height) y22[(2 * yy) * width + 2 * xx] = data[pos];
                if (2 * xx < width && 2 * yy + 1 < height) y22[(2 * yy + 1) * width + 2 * xx] = data[pos + 1];
                if (2 * xx + 1 < width && 2 * yy < height) y22[(2 * yy) * width + 2 * xx + 1] = data[pos + 2];
                if (2 * xx + 1 < width && 2 * yy + 1 < height) y22[(2 * yy + 1) * width + 2 * xx + 1] = data[pos + 3];
                u11[pixel] = data[pos + 4];
                v11[pixel] = data[pos + 5];
                ++pixel;
                pos += 6;
            }
        } else {
            if (pos + 6 > limit) {
                break;
            }
            u8 chunk[6] = { data[pos], data[pos + 1], data[pos + 2], data[pos + 3], data[pos + 4], data[pos + 5] };
            pos += 6;
            for (size_t n = 0; n < count && pixel < maxPixels; ++n) {
                size_t yy = pixel / uvW;
                size_t xx = pixel % uvW;
                if (2 * xx < width && 2 * yy < height) y22[(2 * yy) * width + 2 * xx] = chunk[0];
                if (2 * xx < width && 2 * yy + 1 < height) y22[(2 * yy + 1) * width + 2 * xx] = chunk[1];
                if (2 * xx + 1 < width && 2 * yy < height) y22[(2 * yy) * width + 2 * xx + 1] = chunk[2];
                if (2 * xx + 1 < width && 2 * yy + 1 < height) y22[(2 * yy + 1) * width + 2 * xx + 1] = chunk[3];
                u11[pixel] = chunk[4];
                v11[pixel] = chunk[5];
                ++pixel;
            }
        }
    }

    out->width = (u32)width;
    out->height = (u32)height;
    out->pixels.assign(width * height, 0);
    std::vector<u8> rows(uvW * 2);
    u8* u = rows.data();
    u8* v = u + uvW;
    ImageDrawInfo bounds;
    bounds.left = (u32)width; bounds.top = (u32)height;
    bounds.allPixelsVisible = true;
    for (size_t y = 0; y < height; ++y) {
        chromaRow(u11, uvW, uvH, y, u);
        chromaRow(v11, uvW, uvH, y, v);
        const u8* luma = y22.data() + y * width;
        u32* dest = out->pixels.data() + y * width;
        size_t left = width, right = 0;
        for (size_t x = 0; x < width; ++x) {
            if (luma[x] == 0) {
                bounds.allPixelsVisible = false;
                continue;
            }
            const size_t cx = x / 2;
            const size_t neighbor = (x & 1) ? std::min(cx + 1, uvW - 1) : (cx ? cx - 1 : 0);
            int c = (int)luma[x] - 16;
            const u8 chromaU = u[cx] ? u[cx] : u[neighbor];
            const u8 chromaV = v[cx] ? v[cx] : v[neighbor];
            // Zero marks unavailable chroma in Native32's neighbor recovery.
            // If neither pass found a value (e.g. a black dithered shadow),
            // keep neutral chroma instead of turning Y=16 into green #009a00.
            // Apply this only after both passes, preserving colored neighbors
            // and the separate Y=0 transparency rule above.
            int d = chromaU ? (int)chromaU - 128 : 0;
            int e = chromaV ? (int)chromaV - 128 : 0;
            u8 r = clip((298 * c + 409 * e + 128) >> 8);
            u8 g = clip((298 * c - 100 * d - 208 * e + 128) >> 8);
            u8 b = clip((298 * c + 516 * d + 128) >> 8);
            dest[x] = 0xff000000u | ((u32)r << 16) | ((u32)g << 8) | b;
            if (info) { left = std::min(left, x); right = x + 1; }
        }
        if (info && right) {
            bounds.left = std::min(bounds.left, (u32)left);
            bounds.right = std::max(bounds.right, (u32)right);
            bounds.top = std::min(bounds.top, (u32)y);
            bounds.bottom = (u32)y + 1;
        }
    }
    if (info) *info = bounds;
    return true;
}

static u32 argb1555ToArgb(u16 value) {
    if ((value & 0x8000) == 0) {
        return 0;
    }
    u32 r = ((value >> 10) & 0x1f) << 3;
    u32 g = ((value >> 5) & 0x1f) << 3;
    u32 b = (value & 0x1f) << 3;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

bool decodeImageArgb(const std::vector<u8>& data, RgbaImage* out) {
    return decodeImageArgb(data.data(), data.size(), out);
}

bool decodeImageArgb(const u8* data, size_t size, RgbaImage* out, ImageDrawInfo* info) {
    if (!out || !data || size < 8) {
        return false;
    }
    size_t width = read_u16_le(&data[0], 0);
    size_t height = read_u16_le(&data[0], 2);
    size_t imageSize = read_u32_le(&data[0], 4);
    if (!validRasterSize((u32)width, (u32)height) || imageSize > size - 8) {
        return false;
    }
    size_t total = width * height;
    out->width = (u32)width;
    out->height = (u32)height;
    out->pixels.assign(total, 0);

    size_t pixel = 0;
    size_t pos = 8;
    size_t limit = imageSize + 8;
    ImageDrawInfo bounds;
    bounds.left = (u32)width; bounds.top = (u32)height;
    bounds.allPixelsVisible = true;
    while (pos + 2 <= limit && pixel < total) {
        u16 op = read_u16_le(&data[0], pos);
        if (op == 0) {
            out->pixels[pixel++] = 0;
            bounds.allPixelsVisible = false;
            pos += 2;
        } else if ((op & 0xc000) == 0xc000) {
            size_t count = op & 0x3fff;
            if (pos + 4 > limit) {
                break;
            }
            u32 argb = argb1555ToArgb(read_u16_le(&data[0], pos + 2));
            const size_t end = pixel + std::min(count, total - pixel);
            if (info && end != pixel) {
                if (argb) {
                    const u32 y0 = (u32)(pixel / width), y1 = (u32)((end - 1) / width);
                    const u32 x0 = y0 == y1 ? (u32)(pixel % width) : 0;
                    const u32 x1 = y0 == y1 ? (u32)((end - 1) % width + 1) : (u32)width;
                    bounds.left = std::min(bounds.left, x0); bounds.right = std::max(bounds.right, x1);
                    bounds.top = std::min(bounds.top, y0); bounds.bottom = y1 + 1;
                } else bounds.allPixelsVisible = false;
            }
            std::fill(out->pixels.begin() + pixel, out->pixels.begin() + end, argb);
            pixel = end;
            pos += 4;
        } else {
            return false;
        }
    }
    if (pixel != total) bounds.allPixelsVisible = false;
    if (info) *info = bounds;
    return true;
}

}
