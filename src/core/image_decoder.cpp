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

static std::vector<u8> interpolateY(const std::vector<u8>& data, size_t w, size_t h) {
    size_t h1 = h * 2;
    std::vector<u8> result(w * h1, 0);
    for (size_t y = 0; y < h; ++y) {
        for (size_t dy = 0; dy < 2; ++dy) {
            size_t y1 = y * 2 + dy;
            for (size_t x = 0; x < w; ++x) {
                size_t i = y * w + x;
                u8 cur = i < data.size() ? data[i] : 0;
                u8 value;
                if (dy == 0) {
                    value = (y == 0 || cur != 0) ? cur : data[(y - 1) * w + x];
                } else {
                    value = (y == h - 1 || cur != 0) ? cur : data[(y + 1) * w + x];
                }
                result[y1 * w + x] = value;
            }
        }
    }
    return result;
}

static std::vector<u8> interpolateX(const std::vector<u8>& data, size_t w, size_t h) {
    size_t w1 = w * 2;
    std::vector<u8> result(w1 * h, 0);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t i = y * w + x;
            u8 cur = i < data.size() ? data[i] : 0;
            for (size_t dx = 0; dx < 2; ++dx) {
                u8 value;
                if (dx == 0) {
                    value = (x == 0 || cur != 0) ? cur : data[y * w + (x - 1)];
                } else {
                    value = (x == w - 1 || cur != 0) ? cur : data[y * w + (x + 1)];
                }
                result[y * w1 + x * 2 + dx] = value;
            }
        }
    }
    return result;
}

bool decodeImageYuv(const std::vector<u8>& data, RgbaImage* out) {
    if (!out || data.size() < 8) {
        return false;
    }
    size_t width = read_u16_le(&data[0], 0);
    size_t height = read_u16_le(&data[0], 2);
    size_t imageSize = read_u32_le(&data[0], 4);
    if (!validRasterSize((u32)width, (u32)height) || imageSize > data.size() - 8) {
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
    size_t limit = std::min(data.size(), imageSize + 8);

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

    std::vector<u8> u22 = interpolateX(interpolateY(u11, uvW, uvH), uvW, height);
    std::vector<u8> v22 = interpolateX(interpolateY(v11, uvW, uvH), uvW, height);
    size_t chromaW = uvW * 2;

    out->width = (u32)width;
    out->height = (u32)height;
    out->pixels.assign(width * height, 0);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            size_t li = y * width + x;
            if (y22[li] == 0) {
                out->pixels[li] = 0;
                continue;
            }
            size_t ci = y * chromaW + std::min(x, chromaW - 1);
            int c = (int)y22[li] - 16;
            int d = (int)(ci < u22.size() ? u22[ci] : 128) - 128;
            int e = (int)(ci < v22.size() ? v22[ci] : 128) - 128;
            u8 r = clip((298 * c + 409 * e + 128) >> 8);
            u8 g = clip((298 * c - 100 * d - 208 * e + 128) >> 8);
            u8 b = clip((298 * c + 516 * d + 128) >> 8);
            out->pixels[li] = 0xff000000u | ((u32)r << 16) | ((u32)g << 8) | b;
        }
    }
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
    if (!out || data.size() < 8) {
        return false;
    }
    size_t width = read_u16_le(&data[0], 0);
    size_t height = read_u16_le(&data[0], 2);
    size_t imageSize = read_u32_le(&data[0], 4);
    if (!validRasterSize((u32)width, (u32)height) || imageSize > data.size() - 8) {
        return false;
    }
    size_t total = width * height;
    out->width = (u32)width;
    out->height = (u32)height;
    out->pixels.assign(total, 0);

    size_t pixel = 0;
    size_t pos = 8;
    size_t limit = std::min(data.size(), imageSize + 8);
    while (pos + 2 <= limit && pixel < total) {
        u16 op = read_u16_le(&data[0], pos);
        if (op == 0) {
            out->pixels[pixel++] = 0;
            pos += 2;
        } else if ((op & 0xc000) == 0xc000) {
            size_t count = op & 0x3fff;
            if (pos + 4 > limit) {
                break;
            }
            u32 argb = argb1555ToArgb(read_u16_le(&data[0], pos + 2));
            for (size_t i = 0; i < count && pixel < total; ++i) {
                out->pixels[pixel++] = argb;
            }
            pos += 4;
        } else {
            return false;
        }
    }
    return true;
}

}
