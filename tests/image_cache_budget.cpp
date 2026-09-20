#include "core/native32_reader.h"
#include "core/renderer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <utility>

using namespace n32;

static const size_t MiB = 1024u * 1024u;

struct ImageSpec {
    unsigned width, height, seed;
    bool transparent;
    ImageSpec(unsigned w, unsigned h, unsigned color, bool holes = false)
        : width(w), height(h), seed(color), transparent(holes) {}
};

static void put16(std::vector<u8>& bytes, size_t at, unsigned value) {
    bytes[at] = (u8)value;
    bytes[at + 1] = (u8)(value >> 8);
}

static void put32(std::vector<u8>& bytes, size_t at, size_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = (u8)(value >> (i * 8));
}

static u16 encodedPixel(const ImageSpec& image, unsigned x, unsigned y) {
    if (image.transparent &&
        (x < 16 || x >= image.width - 16 || y < 16 || y >= image.height - 16 ||
         (x >= 128 && x < 192 && y >= 64 && y < 256))) return 0;
    return (u16)(0x8000 | ((image.seed * 997 + y * 31) & 0x7fff));
}

static u32 expectedPixel(const ImageSpec& image, unsigned x, unsigned y) {
    const u16 color = encodedPixel(image, x, y);
    if (!color) return 0;
    return 0xff000000u | (((color >> 10) & 31u) << 19) |
           (((color >> 5) & 31u) << 11) | ((color & 31u) << 3);
}

static std::vector<ImageSpec> imageSpecs(unsigned count) {
    std::vector<ImageSpec> images;
    for (unsigned i = 0; i < count; ++i) images.push_back(ImageSpec(512, 512, i + 1));
    return images;
}

// Real compressed ARGB images, with a file offset table and optional unused
// file tail. A large retained capacity with a short logical size models packed
// assets without requiring copyrighted game fixtures or private reader access.
static std::vector<u8> fixture(const std::vector<ImageSpec>& images,
                               size_t retainedBytes = 0, bool shortSize = false) {
    std::vector<u8> bytes(images.size() * 4, 0);
    for (size_t id = 0; id < images.size(); ++id) {
        const ImageSpec& image = images[id];
        const size_t start = bytes.size();
        put32(bytes, id * 4, start);
        bytes.resize(start + 8, 0);
        put16(bytes, start, image.width); put16(bytes, start + 2, image.height);
        for (unsigned y = 0; y < image.height; ++y) {
            for (unsigned x = 0; x < image.width;) {
                const u16 color = encodedPixel(image, x, y);
                unsigned count = 1;
                while (x + count < image.width && count < 0x3fff &&
                       encodedPixel(image, x + count, y) == color) ++count;
                const size_t at = bytes.size();
                if (color) {
                    bytes.resize(at + 4, 0);
                    put16(bytes, at, 0xc000 | count); put16(bytes, at + 2, color);
                } else bytes.resize(at + count * 2, 0);
                x += count;
            }
        }
        put32(bytes, start + 4, bytes.size() - start - 8);
    }
    if (retainedBytes) {
        assert(bytes.size() < retainedBytes);
        std::vector<u8> retained(retainedBytes, 0);
        std::copy(bytes.begin(), bytes.end(), retained.begin());
        if (shortSize) retained.resize(bytes.size());
        return retained;
    }
    return bytes;
}

static void load(Native32Reader& reader, const std::vector<ImageSpec>& images,
                 size_t retainedBytes = 0, bool shortSize = false) {
    reader.setData(fixture(images, retainedBytes, shortSize));
    reader.colorspace = ColorspaceArgb;
}

static void verifyImage(const RgbaImage* actual, const ImageSpec& expected) {
    assert(actual && actual->width == expected.width && actual->height == expected.height);
    assert(actual->pixels.size() == (size_t)expected.width * expected.height);
    for (unsigned y = 0; y < expected.height; ++y)
        for (unsigned x = 0; x < expected.width; ++x)
            assert(actual->pixels[(size_t)y * expected.width + x] == expectedPixel(expected, x, y));
}

static void testSmallFileAndLru() {
    Native32Reader reader;
    const std::vector<ImageSpec> images = imageSpecs(7);
    load(reader, images);
    assert(reader.data.capacity() < MiB);
    for (unsigned id = 1; id <= 6; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
    assert(reader.imageCacheBytes() == 6 * MiB && reader.imageCacheCount() == 6);
    assert(reader.imageCacheEvictions() == 0 && reader.imageDecodeCount() == 6);
    verifyImage(reader.getImageRef(1), images[0]); // Keep image 1 newer than image 2.
    verifyImage(reader.getImageRef(7), images[6]);
    assert(reader.imageCacheBytes() == 6 * MiB && reader.imageCacheEvictions() == 1);
    const u32 decoded = reader.imageDecodeCount();
    verifyImage(reader.getImageRef(1), images[0]);
    assert(reader.imageDecodeCount() == decoded);
    verifyImage(reader.getImageRef(2), images[1]);
    assert(reader.imageDecodeCount() == decoded + 1 && reader.imageCacheEvictions() == 2);
    // The pressure policy must not shrink a fitting scene such as CMPLAY.
    for (size_t retained : {8 * MiB, 9 * MiB, 10 * MiB}) {
        load(reader, images, retained);
        for (unsigned id = 1; id <= 6; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
        assert(reader.imageCacheBytes() == 6 * MiB && reader.imageCacheEvictions() == 0);
    }
}

static void testLargeFileAndLru() {
    Native32Reader reader;
    const std::vector<ImageSpec> images = imageSpecs(4);
    load(reader, images, 11 * MiB);
    assert(reader.data.capacity() == 11 * MiB);
    for (unsigned id = 1; id <= 3; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
    assert(reader.imageCacheBytes() == 3 * MiB && reader.imageCacheEvictions() == 0);
    verifyImage(reader.getImageRef(1), images[0]);
    verifyImage(reader.getImageRef(4), images[3]);
    // This assertion fails with the old fixed 6 MiB image budget.
    assert(reader.imageCacheBytes() == 3 * MiB && reader.imageCacheEvictions() == 1);
    const u32 decoded = reader.imageDecodeCount();
    verifyImage(reader.getImageRef(1), images[0]);
    assert(reader.imageDecodeCount() == decoded);
    verifyImage(reader.getImageRef(2), images[1]);
    assert(reader.imageDecodeCount() == decoded + 1 && reader.imageCacheEvictions() == 2);
}

static void testRetainedCapacityAndBudgetFloor() {
    Native32Reader reader;
    const std::vector<ImageSpec> images = imageSpecs(3);
    load(reader, images, 12 * MiB, true);
    assert(reader.data.size() < MiB && reader.data.capacity() == 12 * MiB);
    for (unsigned id = 1; id <= 3; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
    assert(reader.imageCacheBytes() == 2 * MiB && reader.imageCacheEvictions() == 1);

    // A file at/beyond the combined cap must not wrap an unsigned subtraction.
    for (size_t retained : {13 * MiB, 14 * MiB, 16 * MiB}) {
        load(reader, images, retained, true);
        for (unsigned id = 1; id <= 3; ++id) {
            verifyImage(reader.getImageRef(id), images[id - 1]);
            assert(reader.imageCacheBytes() == MiB && reader.imageCacheCount() == 1);
        }
        assert(reader.imageCacheEvictions() == 2);
    }
}

static void testLargerSingleImage() {
    Native32Reader reader;
    std::vector<ImageSpec> images = imageSpecs(2);
    images[0] = ImageSpec(768, 512, 17, true); // 1.5 MiB exceeds the 1 MiB budget.
    load(reader, images, 13 * MiB);
    verifyImage(reader.getImageRef(1), images[0]);
    assert(reader.imageCacheBytes() == 3 * MiB / 2 && reader.imageCacheCount() == 1);
    const u32 decoded = reader.imageDecodeCount();
    verifyImage(reader.getImageRef(1), images[0]);
    assert(reader.imageDecodeCount() == decoded);
    verifyImage(reader.getImageRef(2), images[1]);
    assert(reader.imageCacheBytes() == MiB && reader.imageCacheCount() == 1);
    verifyImage(reader.getImageRef(1), images[0]);
    assert(reader.imageCacheBytes() == 3 * MiB / 2 && reader.imageCacheCount() == 1);
    assert(reader.imageDecodeCount() == decoded + 2 && reader.imageCacheEvictions() == 2);
}

static void testPixelsAfterEviction() {
    Native32Reader reader;
    std::vector<ImageSpec> images = imageSpecs(2);
    images[0].transparent = true;
    load(reader, images, 13 * MiB);
    Renderer renderer(531, 529);
    SpriteSystem sprites;
    std::vector<FrameObject> objects(1);
    objects[0].index = 1; objects[0].x = 9; objects[0].y = 11;
    std::vector<u32> expected(531 * 529, 0xff000000);
    for (unsigned y = 0; y < images[0].height; ++y)
        for (unsigned x = 0; x < images[0].width; ++x) {
            const u32 color = expectedPixel(images[0], x, y);
            if (color) expected[(y + 11) * 531 + x + 9] = color;
        }
    for (unsigned pass = 0; pass < 3; ++pass) {
        std::fill(renderer.buffer.begin(), renderer.buffer.end(), 0xffabcdef);
        renderer.drawFrame(&reader, sprites, objects);
        assert(renderer.buffer == expected);
        bool opaque = true;
        const ImageDrawInfo* bounds = 0;
        verifyImage(reader.getImageRef(1, &opaque, &bounds), images[0]);
        assert(!opaque && bounds && bounds->left == 16 && bounds->top == 16 &&
               bounds->right == 496 && bounds->bottom == 496);
        verifyImage(reader.getImageRef(2), images[1]);
    }
    assert(reader.imageDecodeCount() == 6 && reader.imageCacheEvictions() == 5);
}

static void testReloadRestoresBudget() {
    Native32Reader reader;
    std::vector<ImageSpec> images = imageSpecs(7);
    load(reader, images, 13 * MiB);
    for (unsigned id = 1; id <= 3; ++id) assert(reader.getImageRef(id));
    assert(reader.imageCacheBytes() == MiB && reader.imageCacheEvictions() == 2);
    images[0].seed = 71;
    load(reader, images);
    assert(reader.imageCacheBytes() == 0 && reader.imageCacheCount() == 0 &&
           reader.imageCacheEvictions() == 0 && reader.imageDecodeCount() == 0);
    for (unsigned id = 1; id <= 6; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
    assert(reader.imageCacheBytes() == 6 * MiB && reader.imageCacheEvictions() == 0);
    load(reader, images, 13 * MiB);
    assert(reader.imageCacheBytes() == 0 && reader.imageDecodeCount() == 0);
    for (unsigned id = 1; id <= 2; ++id) verifyImage(reader.getImageRef(id), images[id - 1]);
    assert(reader.imageCacheBytes() == MiB && reader.imageCacheEvictions() == 1);
}

int main() {
    testSmallFileAndLru();
    testLargeFileAndLru();
    testRetainedCapacityAndBudgetFloor();
    testLargerSingleImage();
    testPixelsAfterEviction();
    testReloadRestoresBudget();
    std::puts("image cache budget: small/large files, LRU, retained capacity, floor, oversized image, pixels and reload passed");
}
