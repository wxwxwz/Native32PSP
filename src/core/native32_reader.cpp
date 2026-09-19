#include "core/actions.h"
#include "core/native32_reader.h"
#include "core/endian.h"
#include "core/header_decryptor.h"
#include "core/image_decoder.h"
#include "core/raster_limits.h"
#include <utility>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

namespace n32 {

static bool fileRange(size_t size, u64 start, size_t count, size_t* offset) {
    if (start > size || count > size - (size_t)start) return false;
    *offset = (size_t)start;
    return true;
}

ObjectType objectTypeFromU16(u16 value, bool* ok) {
    if (ok) {
        *ok = true;
    }
    switch (value) {
    case 1: return ObjectImage;
    case 2: return ObjectMovie;
    case 3: return ObjectButton;
    case 4: return ObjectAction;
    case 5: return ObjectSound;
    default:
        if (ok) {
            *ok = false;
        }
        return ObjectImage;
    }
}

Native32Reader::Native32Reader()
    : idx(0), colorspace(ColorspaceYuv), width(320), height(240), base(0),
      frameIdx(0), imageIdx(0), actionIdx(0), movieIdx(0), buttonIdx(0),
      buttonCondIdx(0), mp3Offset(0), soundTable(0), cachedImageBytes(0), imageClock(0), imageEvictions(0) {
    actionsCache.resize(1);
}

Native32Reader::Native32Reader(const std::vector<u8>& fileData) : Native32Reader() {
    setData(fileData);
}

void Native32Reader::setData(std::vector<u8> fileData) {
    data = std::move(fileData);
    idx = 0;
    colorspace = ColorspaceYuv;
    width = 320;
    height = 240;
    base = 0;
    frameIdx = imageIdx = actionIdx = movieIdx = buttonIdx = buttonCondIdx = 0;
    mp3Offset = 0;
    soundTable = 0;
    decltype(actionsCache)().swap(actionsCache);
    actionsCache.resize(1);
    imagesCache.clear();
    imageLastUsed.clear();
    cachedImageBytes = 0;
    imageClock = imageEvictions = 0;
    imageValidCache.clear();
    framesCache.clear();
    moviesCache.clear();
    soundCache.clear();
    buttonEventsCache.clear();
}

std::string Native32Reader::getStr(size_t offset) const {
    std::string out;
    size_t pos = offset;
    while (pos < data.size() && data[pos] != 0) {
        out.push_back((char)data[pos]);
        ++pos;
    }
    return out;
}

void Native32Reader::skipThumbnail() {
    if (idx + 4 <= data.size() && memcmp(&data[idx], "SWFT", 4) == 0) {
        idx += 4;
        if (idx + 0x10 <= data.size()) {
            size_t size = read_u32_le(&data[0], idx + 0x0c);
            idx += 0x10 + size;
            if (idx > data.size()) {
                idx = data.size();
            }
        }
    }
}

bool Native32Reader::findHeader() {
    while (idx + 4 <= data.size()) {
        if (memcmp(&data[idx], "_YUV", 4) == 0) {
            colorspace = ColorspaceYuv;
            return true;
        }
        if (memcmp(&data[idx], "ARGB", 4) == 0) {
            colorspace = ColorspaceArgb;
            return true;
        }
        ++idx;
    }
    return false;
}

bool parseResolution(const std::string& generator, u32* outWidth, u32* outHeight) {
    static const char prefix[] = "Resolution_";
    if (generator.compare(0, sizeof(prefix) - 1, prefix) != 0) {
        return false;
    }
    size_t pos = sizeof(prefix) - 1;
    size_t sep = generator.find('_', pos);
    if (sep == std::string::npos) {
        return false;
    }
    long w = strtol(generator.substr(pos, sep - pos).c_str(), 0, 10);
    long h = strtol(generator.substr(sep + 1).c_str(), 0, 10);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 ||
        !validRasterSize((u32)w, (u32)h)) {
        return false;
    }
    if (outWidth) {
        *outWidth = (u32)w;
    }
    if (outHeight) {
        *outHeight = (u32)h;
    }
    return true;
}

bool Native32Reader::processHeader() {
    size_t genStart = idx + 0x04;
    if (genStart + 0x30 <= data.size()) {
        std::string generator;
        for (size_t i = 0; i < 0x30 && data[genStart + i] != 0; ++i) {
            generator.push_back((char)data[genStart + i]);
        }
        u32 parsedWidth = 0;
        u32 parsedHeight = 0;
        if (generator.compare(0, 11, "Resolution_") == 0 &&
            !parseResolution(generator, &parsedWidth, &parsedHeight)) {
            return false;
        }
        if (parsedWidth > 0 && parsedHeight > 0) {
            width = parsedWidth;
            height = parsedHeight;
        }
    }

    base = idx + 0x60;
    if (base + 0x38 > data.size()) {
        return false;
    }

    mp3Offset = read_u32_le(&data[0], base + 0x10);

    size_t encStart = base + 0x18;
    if (encStart + 0x20 > data.size()) {
        return false;
    }
    std::vector<u8> decrypted;
    if (!decryptHeader(&data[encStart], 0x20, &decrypted) || decrypted.size() < 0x20) {
        return false;
    }

    frameIdx = read_u32_le(&decrypted[0], 0x08);
    imageIdx = read_u32_le(&decrypted[0], 0x0c);
    actionIdx = read_u32_le(&decrypted[0], 0x10);
    movieIdx = read_u32_le(&decrypted[0], 0x14);
    buttonIdx = read_u32_le(&decrypted[0], 0x18);
    buttonCondIdx = read_u32_le(&decrypted[0], 0x1c);

    if (!expandPackedAssets(read_u16_le(&data[0], base) & 0xff,
                            read_u32_le(&data[0], base + 0x0c))) return false;

    size_t cursorPos = encStart + 0x20;
    if (cursorPos + 4 <= data.size()) {
        size_t cursorW = read_u16_le(&data[0], cursorPos);
        size_t cursorH = read_u16_le(&data[0], cursorPos + 2);
        soundTable = cursorPos + 4 + 2 * cursorW * cursorH;
    }

    return true;
}

bool Native32Reader::expandPackedAssets(size_t blockCount, u32 binarySize) {
    if (!blockCount) return true;
    size_t table;
    if (blockCount > 255 || !fileRange(data.size(), (u64)base + imageIdx, 4, &table)) return false;
    u32 first = read_u32_le(&data[0], table);
    // Bound expanded file size before allocating on the PSP's limited heap.
    const size_t maxFile = 16u * 1024u * 1024u;
    if (first == 0xffffffffu || first > binarySize || (u64)base + binarySize > maxFile) return false;
    size_t assetStart;
    if (!fileRange(data.size(), (u64)base + first, 0, &assetStart)) return false;
    u64 blockStart = (u64)base + (((u64)first + 0x7ff) & ~0x7ffull);
    const size_t capacity = base + binarySize;
    // Build the final file directly so commit does not allocate a second copy
    // of the entire decompressed asset set on the PSP heap.
    std::vector<u8> expanded(data.begin(), data.begin() + assetStart);
    u8 chunk[4096];
    for (size_t i = 0; i < blockCount; ++i) {
        size_t start;
        if (!fileRange(data.size(), blockStart, 4, &start)) return false;
        u32 size = read_u32_le(&data[0], start);
        if (size <= 4 || !fileRange(data.size(), blockStart, size, &start)) return false;
        z_stream stream = {};
        stream.next_in = &data[start + 4];
        stream.avail_in = size - 4;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
        int result = Z_OK;
        do {
            stream.next_out = chunk;
            stream.avail_out = sizeof(chunk);
            result = inflate(&stream, Z_NO_FLUSH);
            size_t produced = sizeof(chunk) - stream.avail_out;
            if ((result != Z_OK && result != Z_STREAM_END) || produced > capacity - expanded.size()) {
                inflateEnd(&stream);
                return false;
            }
            size_t needed = expanded.size() + produced;
            if (needed > expanded.capacity()) {
                size_t growth = std::max(needed, expanded.capacity() * 2);
                expanded.reserve(std::min(capacity, growth));
            }
            expanded.insert(expanded.end(), chunk, chunk + produced);
        } while (result != Z_STREAM_END);
        inflateEnd(&stream);
        blockStart += size;
    }
    if (blockStart != data.size()) return false;
    // Mutate only after every block is validated, retaining the original prefix.
    data.swap(expanded);
    return true;
}

bool Native32Reader::init() {
    skipThumbnail();
    return findHeader() && processHeader();
}

bool Native32Reader::disassembleAction(u32 index, ActionEntry* out) const {
    if (index == 0 || !out) {
        return false;
    }
    size_t ptr;
    if (!fileRange(data.size(), (u64)base + actionIdx + (u64)(index - 1) * 8, 8, &ptr)) {
        return false;
    }
    u32 opcode = read_u32_le(&data[0], ptr);
    u32 payloadVal = read_u32_le(&data[0], ptr + 4);
    Action action;
    if (!actionFromU32(opcode, &action)) {
        return false;
    }

    ActionEntry entry;
    entry.opcode = opcode;
    entry.action = action;

    if (payloadVal != 0 && action != ActionEnd) {
        size_t payloadIdx;
        if (fileRange(data.size(), (u64)base + payloadVal, 1, &payloadIdx)) {
            entry.payload.hasPayload = true;
            if (action == ActionIf || action == ActionGotoFrame || action == ActionGotoFrame2 || action == ActionJump) {
                if (payloadIdx + 2 <= data.size()) {
                    entry.payload.isInteger = true;
                    entry.payload.integer = read_i16_le(&data[0], payloadIdx);
                } else {
                    entry.payload.hasPayload = false;
                }
            } else {
                entry.payload.isInteger = false;
                entry.payload.text = getStr(payloadIdx);
            }
        }
    }

    *out = entry;
    return true;
}

bool Native32Reader::getAction(u32 index, ActionEntry* out) {
    if (index == 0 || !out) {
        return false;
    }
    // A negative branch can wrap to UINT_MAX. Never grow the cache for an
    // index that cannot fit in the file (subtractions avoid 32-bit wrap).
    if (base > data.size() || actionIdx > data.size() - base) return false;
    const size_t available = data.size() - base - actionIdx;
    if (available < 8 || (size_t)(index - 1) > (available - 8) / 8) return false;
    while ((size_t)index >= actionsCache.size()) {
        u32 i = (u32)actionsCache.size();
        ActionCacheEntry cacheEntry;
        cacheEntry.valid = disassembleAction(i, &cacheEntry.entry);
        actionsCache.push_back(cacheEntry);
    }
    if (!actionsCache[index].valid) {
        return false;
    }
    *out = actionsCache[index].entry;
    return true;
}

bool Native32Reader::getActionCached(u32 index, ActionEntry* out) const {
    if (!out || index >= actionsCache.size() || !actionsCache[index].valid) {
        return false;
    }
    *out = actionsCache[index].entry;
    return true;
}

bool Native32Reader::getFrame(u32 frame, std::vector<FrameObject>* out) {
    if (frame == 0 || !out) {
        return false;
    }
    std::map<u32, std::vector<FrameObject> >::const_iterator cached = framesCache.find(frame);
    if (cached != framesCache.end()) {
        *out = cached->second;
        return true;
    }

    size_t ptrIdx;
    if (!fileRange(data.size(), (u64)base + frameIdx + 4ull * (frame - 1), 4, &ptrIdx)) {
        return false;
    }
    u32 offset = read_u32_le(&data[0], ptrIdx);
    if (offset == 0 || (size_t)offset > data.size()) {
        return false;
    }

    std::vector<FrameObject> objects;
    size_t pos;
    if (!fileRange(data.size(), (u64)base + offset, 0, &pos)) return false;
    while (pos + 0x10 <= data.size()) {
        u16 rawType = read_u16_le(&data[0], pos);
        if (rawType == 0x0000 || rawType == 0xffff) {
            break;
        }
        bool ok = false;
        ObjectType type = objectTypeFromU16(rawType, &ok);
        if (ok) {
            FrameObject object;
            object.type = type;
            object.index = read_u16_le(&data[0], pos + 2);
            object.x = read_i16_le(&data[0], pos + 4);
            object.y = read_i16_le(&data[0], pos + 6);
            object.depth = read_u16_le(&data[0], pos + 8);
            u32 namePtr = read_u32_le(&data[0], pos + 12);
            if (namePtr != 0) {
                object.hasName = true;
                object.name = getStr(base + (size_t)namePtr);
            }
            objects.push_back(object);
        }
        pos += 0x10;
    }

    framesCache[frame] = objects;
    *out = objects;
    return true;
}

const std::vector<FrameObject>* Native32Reader::getFrameRef(u32 frame) {
    if (frame == 0) {
        return 0;
    }
    std::map<u32, std::vector<FrameObject> >::const_iterator cached = framesCache.find(frame);
    if (cached == framesCache.end()) {
        std::vector<FrameObject> unused;
        if (!getFrame(frame, &unused)) {
            return 0;
        }
        cached = framesCache.find(frame);
    }
    return cached == framesCache.end() ? 0 : &cached->second;
}

void Native32Reader::getMovie(u32 movie, std::vector<MovieFrame>* out) {
    if (!out) {
        return;
    }
    out->clear();
    if (movie == 0) {
        return;
    }
    std::map<u32, std::vector<MovieFrame> >::const_iterator cached = moviesCache.find(movie);
    if (cached != moviesCache.end()) {
        *out = cached->second;
        return;
    }

    size_t idxPtr;
    if (!fileRange(data.size(), (u64)base + movieIdx + 4ull * (movie - 1), 4, &idxPtr)) {
        return;
    }
    size_t pos;
    if (!fileRange(data.size(), (u64)base + read_u32_le(&data[0], idxPtr), 0, &pos)) return;
    while (pos + 0x0c <= data.size()) {
        u16 image = read_u16_le(&data[0], pos);
        if (image == 0xffff || image == 0x0000) {
            break;
        }
        if ((size_t)image >= data.size()) {
            break;
        }
        MovieFrame frame;
        frame.image = image;
        frame.x = read_i16_le(&data[0], pos + 2);
        frame.y = read_i16_le(&data[0], pos + 4);
        frame.action = read_u16_le(&data[0], pos + 6);
        frame.sound = read_u16_le(&data[0], pos + 8);
        frame.reserved = read_u16_le(&data[0], pos + 10);
        out->push_back(frame);
        pos += 0x0c;
    }
    moviesCache[movie] = *out;
}

const std::vector<MovieFrame>* Native32Reader::getMovieRef(u32 movie) {
    if (movie == 0) {
        return 0;
    }
    std::map<u32, std::vector<MovieFrame> >::const_iterator cached = moviesCache.find(movie);
    if (cached == moviesCache.end()) {
        std::vector<MovieFrame> unused;
        getMovie(movie, &unused);
        cached = moviesCache.find(movie);
    }
    return cached == moviesCache.end() ? 0 : &cached->second;
}

bool Native32Reader::getImage(u32 index, RgbaImage* out) {
    if (!out) return false;
    const RgbaImage* image = getImageRef(index);
    if (!image) return false;
    *out = *image;
    return true;
}

const RgbaImage* Native32Reader::getImageRef(u32 index) {
    if (index == 0) return 0;
    std::map<u32, RgbaImage>::const_iterator cached = imagesCache.find(index);
    if (cached != imagesCache.end()) {
        imageLastUsed[index] = ++imageClock;
        return &cached->second;
    }
    std::map<u32, bool>::const_iterator known = imageValidCache.find(index);
    if (known != imageValidCache.end()) {
        return 0;
    }
    // Only failed lookups live in this small negative cache. Corrupt scripts
    // cannot fill an unbounded map by requesting a fresh invalid index per tick.
    if (imageValidCache.size() >= 256) imageValidCache.clear();

    size_t ptr;
    if (!fileRange(data.size(), (u64)base + imageIdx + 4ull * (index - 1), 4, &ptr)) {
        imageValidCache[index] = false;
        return 0;
    }
    u32 imgOffset = read_u32_le(&data[0], ptr);
    if (imgOffset == 0xffffffffu) {
        imageValidCache[index] = false;
        return 0;
    }
    size_t imgStart;
    if (!fileRange(data.size(), (u64)base + imgOffset, 8, &imgStart)) {
        imageValidCache[index] = false;
        return 0;
    }

    size_t imgSize = read_u32_le(&data[0], imgStart + 4);
    if (imgSize > data.size() - imgStart - 8) {
        imageValidCache[index] = false;
        return 0;
    }
    size_t imgEnd = imgStart + 8 + imgSize;
    u32 width = read_u16_le(&data[0], imgStart);
    u32 height = read_u16_le(&data[0], imgStart + 2);
    if (!validRasterSize(width, height)) { imageValidCache[index] = false; return 0; }
    const size_t bytes = (size_t)width * height * sizeof(u32);
    const size_t budget = 6u * 1024u * 1024u;
    // Evict before decoding so old and new image sets never coexist above the
    // payload budget. Entry count also bounds map overhead for tiny sprites.
    while (!imagesCache.empty() && (cachedImageBytes + bytes > budget || imagesCache.size() >= 512)) {
        std::map<u32, u64>::iterator oldest = imageLastUsed.begin();
        for (std::map<u32, u64>::iterator it = imageLastUsed.begin(); it != imageLastUsed.end(); ++it)
            if (it->second < oldest->second) oldest = it;
        u32 victim = oldest->first;
        cachedImageBytes -= imagesCache.find(victim)->second.pixels.size() * sizeof(u32);
        imagesCache.erase(victim);
        imageLastUsed.erase(oldest);
        ++imageEvictions;
    }
    std::vector<u8> imageData(data.begin() + imgStart, data.begin() + imgEnd);
    RgbaImage image;
    bool ok = colorspace == ColorspaceArgb ? decodeImageArgb(imageData, &image) : decodeImageYuv(imageData, &image);
    if (ok) {
        imagesCache[index] = std::move(image);
        cachedImageBytes += bytes;
        imageLastUsed[index] = ++imageClock;
        return &imagesCache.find(index)->second;
    }
    imageValidCache[index] = false;
    return 0;
}

void endianSwapPcm16(const std::vector<u8>& input, std::vector<u8>* out) {
    if (!out) {
        return;
    }
    size_t len = input.size() & ~(size_t)1;
    out->assign(len, 0);
    for (size_t i = 0; i < len; i += 2) {
        (*out)[i] = input[i + 1];
        (*out)[i + 1] = input[i];
    }
}

bool Native32Reader::getSound(u32 index, SoundData* out) {
    if (!out || index == 0) {
        return false;
    }
    std::map<u32, SoundData>::const_iterator cached = soundCache.find(index);
    if (cached != soundCache.end()) {
        *out = cached->second;
        return true;
    }

    size_t tableIdx;
    if (!fileRange(data.size(), (u64)soundTable + (u64)(index - 1) * 4, 4, &tableIdx)) {
        return false;
    }
    u32 ptr = read_u32_le(&data[0], tableIdx);
    u32 flags = ptr & 0xf0000000u;
    size_t addr = (size_t)(ptr & 0x0fffffffu);

    SoundData result;
    if (flags == 0xf0000000u) {
        size_t begin;
        if (!fileRange(data.size(), (u64)base + mp3Offset + addr, 6, &begin)) {
            return false;
        }
        size_t size = read_u32_le(&data[0], begin);
        size_t dataStart = begin + 6;
        if (size > data.size() - dataStart) {
            return false;
        }
        size_t dataEnd = dataStart + size;
        result.format = AudioMp3;
        result.data.assign(data.begin() + dataStart, data.begin() + dataEnd);
    } else if (flags == 0x00000000u) {
        size_t begin;
        if (!fileRange(data.size(), (u64)base + addr, 4, &begin)) {
            return false;
        }
        size_t size = read_u32_le(&data[0], begin);
        size_t dataStart = begin + 4;
        if (size > data.size() - dataStart) {
            return false;
        }
        size_t dataEnd = dataStart + size;
        result.format = AudioRaw;
        std::vector<u8> raw(data.begin() + dataStart, data.begin() + dataEnd);
        if (colorspace == ColorspaceYuv) {
            endianSwapPcm16(raw, &result.data);
        } else {
            result.data = raw;
        }
    } else {
        return false;
    }

    if(result.format==AudioMp3) { *out=std::move(result);return true; }
    soundCache[index] = result;
    *out = result;
    return true;
}

void Native32Reader::getButtonEvents(u32 button, std::vector<ButtonEvent>* out) {
    if (!out) {
        return;
    }
    out->clear();
    if (button == 0) {
        return;
    }
    std::map<u32, std::vector<ButtonEvent> >::const_iterator cached = buttonEventsCache.find(button);
    if (cached != buttonEventsCache.end()) {
        *out = cached->second;
        return;
    }

    size_t condTableIdx;
    if (!fileRange(data.size(), (u64)base + buttonCondIdx + (u64)(button - 1) * 4, 4, &condTableIdx)) {
        return;
    }
    size_t ptr;
    if (!fileRange(data.size(), (u64)base + read_u32_le(&data[0], condTableIdx), 2, &ptr)) {
        return;
    }
    size_t totalActLen = read_u16_le(&data[0], ptr);
    size_t pos = ptr + 2;
    size_t i = 0;
    while (i < totalActLen && pos + 6 <= data.size()) {
        u16 keycode = read_u16_le(&data[0], pos);
        u16 actLen = read_u16_le(&data[0], pos + 2);
        u16 event = read_u16_le(&data[0], pos + 4);
        out->push_back(ButtonEvent(keycode, event));
        i += actLen;
        pos += 6;
    }
    buttonEventsCache[button] = *out;
}

void Native32Reader::cacheAllActions() {
    u32 i = 1;
    while (true) {
        ActionEntry entry;
        if (!disassembleAction(i, &entry)) {
            break;
        }
        getAction(i, &entry);
        ++i;
    }
}

}
